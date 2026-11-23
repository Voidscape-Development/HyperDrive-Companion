/*
HyperDrive Companion
Copyright (C) 2026 Voidscape Development

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/
#include "paint.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>

#include <QBuffer>
#include <QFontDatabase>
#include <QImageReader>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include "companion.hpp"
#include "core/address.hpp"
#include "core/state.hpp"

namespace hd::bracket {

using json = nlohmann::json;

namespace {

Assets *assets = nullptr;
// Set while painting when an image or the fonts are still loading
thread_local bool missing = false;

// main.css's fonts: Saira Condensed, then Noto Sans for other scripts
const char *kFonts[] = {"layout/fonts/Saira/Saira_Condensed-Bold.ttf",
			"layout/fonts/Saira/Saira_Condensed-SemiBold.ttf",
			"layout/fonts/Saira/Saira_Condensed-Regular.ttf", "layout/fonts/NotoSans/NotoSans.ttf"};

// Sizes in world pixels, as index.css's
// The largest image kept, in pixels
constexpr int kMaxImage = 1024;
constexpr double kRowH = 40, kRowGap = 2, kScoreW = 52, kFlagW = 26, kFlagH = 26 * 0.7, kIdentifierW = 24;

QColor qcolor(uint32_t c)
{
	return QColor::fromRgba(c);
}

QString qs(const std::string &s)
{
	return QString::fromStdString(s);
}

QRectF rectOf(const Box &b)
{
	return {b.x, b.y, b.w, b.h};
}

Box grow(const Box &b, double by)
{
	return {b.x - by, b.y - by, b.w + by * 2, b.h + by * 2};
}

const json &at(const json &value, const char *key)
{
	static const json none;
	return value.is_object() && value.contains(key) ? value[key] : none;
}

const json &at(const json &value, const std::string &key)
{
	return at(value, key.c_str());
}

std::string str(const json &value, const std::string &key)
{
	return text(get(value, key));
}

/** A picture of `area` at a scale, drawn in world pixels */
Sprite paint(const Box &area, double scale, const std::function<void(QPainter &)> &draw)
{
	Sprite sprite;
	sprite.at = area;
	int w = std::max(1, static_cast<int>(std::ceil(area.w * scale)));
	int h = std::max(1, static_cast<int>(std::ceil(area.h * scale)));
	sprite.image = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
	sprite.image.fill(Qt::transparent);
	missing = false;
	{
		QPainter p(&sprite.image);
		p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
		p.scale(w / area.w, h / area.h);
		p.translate(-area.x, -area.y);
		draw(p);
	}
	sprite.incomplete = missing;
	return sprite;
}

/**
 * Draws into a layer of its own, then onto p: what a CSS filter, mask or
 * opacity applies to as a whole. post changes the layer's pixels.
 */
void layer(QPainter &p, double opacity, const std::function<void(QPainter &)> &draw,
	   const std::function<void(QImage &)> &post = {})
{
	QImage *device = static_cast<QImage *>(p.device());
	QImage image(device->size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		QPainter q(&image);
		q.setRenderHints(p.renderHints());
		q.setTransform(p.transform());
		if (p.hasClipping())
			q.setClipPath(p.clipPath());
		draw(q);
	}
	if (post)
		post(image);
	p.save();
	p.resetTransform();
	p.setClipping(false);
	p.setOpacity(p.opacity() * opacity);
	p.drawImage(0, 0, image);
	p.restore();
}

/** CSS filter: brightness() */
void brightness(QImage &image, double factor)
{
	for (int y = 0; y < image.height(); y++) {
		QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
		for (int x = 0; x < image.width(); x++)
			line[x] = qRgba(static_cast<int>(qRed(line[x]) * factor),
					static_cast<int>(qGreen(line[x]) * factor),
					static_cast<int>(qBlue(line[x]) * factor), qAlpha(line[x]));
	}
}

QBrush brush(const Fill &fill, const QRectF &r)
{
	if (!fill.gradient)
		return qcolor(fill.color);
	// CSS's "to right", "to bottom left"... or "<n>deg" (0 is up, clockwise)
	double deg = 180;
	const std::string &d = fill.direction;
	if (d.find("deg") != std::string::npos)
		deg = std::atof(d.c_str());
	else if (d.rfind("to ", 0) == 0) {
		bool top = d.find("top") != std::string::npos, bottom = d.find("bottom") != std::string::npos;
		bool left = d.find("left") != std::string::npos, right = d.find("right") != std::string::npos;
		double x = right - left, y = bottom - top;
		deg = std::atan2(x, -y) * 180 / 3.14159265358979;
	}
	double a = deg * 3.14159265358979 / 180, dx = std::sin(a), dy = -std::cos(a);
	double half = (std::abs(r.width() * dx) + std::abs(r.height() * dy)) / 2;
	QPointF c = r.center(), v(dx * half, dy * half);
	QLinearGradient g(c - v, c + v);
	g.setColorAt(0, qcolor(fill.color));
	g.setColorAt(1, qcolor(fill.color2));
	return g;
}

QFont font(const Theme &theme, double px, bool bold = true, bool italic = false)
{
	QFont f;
	f.setFamilies(assets ? assets->families(theme.font) : QStringList{qs(theme.font)});
	f.setPixelSize(std::max(1, static_cast<int>(std::lround(px))));
	f.setWeight(bold ? QFont::Bold : QFont::Normal);
	f.setItalic(italic);
	// body's letter-spacing
	f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
	f.setHintingPreference(QFont::PreferNoHinting);
	if (theme.upper)
		f.setCapitalization(QFont::AllUppercase);
	return f;
}

/** A piece of text in its own color and font */
struct Run {
	QString text;
	QFont font;
	QColor color;
};

double widthOf(const std::vector<Run> &runs)
{
	double w = 0;
	for (const Run &run : runs)
		w += QFontMetricsF(run.font).horizontalAdvance(run.text);
	return w;
}

/**
 * Text in a box, centered vertically, squeezed to fit its width as HD.set()
 * does, with main.css's faint outline in the opposite color under it.
 */
void drawRuns(QPainter &p, const Theme &theme, const QRectF &r, const std::vector<Run> &runs,
	      Qt::Alignment align = Qt::AlignLeft)
{
	double w = widthOf(runs);
	if (w <= 0)
		return;
	double squeeze = std::min(1.0, r.width() / w);
	double x = align & Qt::AlignHCenter ? r.center().x() - w * squeeze / 2
					    : (align & Qt::AlignRight ? r.right() - w * squeeze : r.left());
	p.save();
	p.translate(x, 0);
	p.scale(squeeze, 1);
	double pen = 0;
	for (const Run &run : runs) {
		QFontMetricsF m(run.font);
		// Centered as a CSS line box is
		double baseline = r.center().y() + (m.ascent() - m.descent()) / 2;
		QString shown = run.font.capitalization() == QFont::AllUppercase ? run.text.toUpper() : run.text;
		QFont f = run.font;
		f.setCapitalization(QFont::MixedCase);
		QPainterPath path;
		path.addText(pen, baseline, f, shown);
		if (theme.textOutline) {
			QColor outline(255 - run.color.red(), 255 - run.color.green(), 255 - run.color.blue(),
				       static_cast<int>(0.15 * run.color.alpha()));
			p.strokePath(path, QPen(outline, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		}
		p.fillPath(path, run.color);
		pen += m.horizontalAdvance(run.text);
	}
	p.restore();
}

void drawText(QPainter &p, const Theme &theme, const QRectF &r, const QString &text, const QFont &f,
	      const QColor &color, Qt::Alignment align = Qt::AlignLeft)
{
	drawRuns(p, theme, r, {{text, f, color}}, align);
}

/** index.css's --bf-shadow, under a rounded box */
void shadow(QPainter &p, const QRectF &r, double radius)
{
	struct {
		double dy, blur, alpha;
	} layers[] = {{2, 2, 0.12}, {3, 1, 0.14}, {1, 5, 0.12}};
	p.save();
	p.setPen(Qt::NoPen);
	for (const auto &l : layers) {
		int steps = std::max(1, static_cast<int>(std::ceil(l.blur)));
		for (int i = 0; i < steps; i++) {
			double by = l.blur * ((i + 0.5) / steps - 0.5);
			p.setBrush(QColor(0, 0, 0, static_cast<int>(255 * l.alpha / steps)));
			p.drawRoundedRect(r.adjusted(-by, -by + l.dy, by, by + l.dy), radius + by, radius + by);
		}
	}
	p.restore();
}

/** An image filling a box (object-fit: cover), or within it (contain) */
void drawImage(QPainter &p, const QRectF &r, const QImage &image, bool cover, double radius = 0)
{
	if (image.isNull())
		return;
	double s = cover ? std::max(r.width() / image.width(), r.height() / image.height())
			 : std::min(r.width() / image.width(), r.height() / image.height());
	QSizeF size(image.width() * s, image.height() * s);
	QRectF target(r.center().x() - size.width() / 2, r.center().y() - size.height() / 2, size.width(),
		      size.height());
	p.save();
	QPainterPath clip;
	clip.addRoundedRect(r, radius, radius);
	p.setClipPath(clip, Qt::IntersectClip);
	p.drawImage(target, image);
	p.restore();
}

QImage image(const std::string &path)
{
	return assets && !path.empty() ? assets->image(path) : QImage();
}

// characters.js: a character's biggest asset, the zoom for uncropped art,
// and HD.centerImage()'s placement on the character's eyesight
const json *biggestAsset(const json &character)
{
	const json *best = nullptr;
	double bestSize = -1;
	for (const json &asset : at(character, "assets")) {
		double size = number(get(asset, "average_size.x")) + number(get(asset, "average_size.y"));
		if (!best || size > bestSize) {
			best = &asset;
			bestSize = size;
		}
	}
	return best;
}

void drawCharacter(QPainter &p, const QRectF &slot, const QRectF &container, const json &asset)
{
	QSize natural;
	QImage img = assets ? assets->image(str(asset, "asset"), &natural) : QImage();
	if (img.isNull())
		return;
	std::string edge = str(asset, "uncropped_edge");
	if (edge == "undefined")
		edge.clear();
	auto has = [&edge](char c) {
		return edge.find(c) != std::string::npos;
	};
	double w = slot.width(), h = slot.height(), nw = natural.width(), nh = natural.height();
	double rescale = number(get(asset, "rescaling_factor"), 1);
	// recommendedZoom(), on the whole container
	double custom = 1;
	if (has('u') && has('d') && has('l') && has('r'))
		custom = 1.2;
	else if (has('l') != has('r') && container.width() > container.height())
		custom = container.width() / container.height();
	else if (has('u') != has('d') && container.height() > container.width())
		custom = container.height() / container.width();
	double avgX = number(get(asset, "average_size.x")), avgY = number(get(asset, "average_size.y"));
	double proportional = avgX > 0 && avgY > 0 ? std::max(w / avgX * 1.2, h / avgY * 1.2) : 1;
	double zoomX = w / nw, zoomY = h / nh, minZoom;
	if (edge.empty())
		minZoom = std::max(zoomX, zoomY) * rescale;
	else if (has('u') && has('d') && has('l') && has('r'))
		minZoom = custom * proportional * rescale;
	else if (!has('l') && !has('r'))
		minZoom = zoomX * rescale;
	else if (!has('u') && !has('d'))
		minZoom = zoomY * rescale;
	else
		minZoom = custom * proportional * rescale;
	double zoom = std::max(minZoom, custom * minZoom);
	auto place = [&](double eye, double size, double full, char low, char high) {
		double pos = std::min(-eye * zoom + size * 0.5, has(low) ? size * 0.1 : 0.0);
		double most = size - full * zoom;
		return std::max(pos, has(high) ? most - size * 0.1 : most);
	};
	const json *eyesight = get(asset, "eyesight");
	double ex = eyesight ? number(get(*eyesight, "x"), nw / 2) : nw / 2;
	double ey = eyesight ? number(get(*eyesight, "y"), nh / 2) : nh / 2;
	p.save();
	p.setClipRect(slot, Qt::IntersectClip);
	p.setOpacity(0.5);
	p.drawImage(QRectF(slot.x() + place(ex, w, nw, 'l', 'r'), slot.y() + place(ey, h, nh, 'u', 'd'), nw * zoom,
			   nh * zoom),
		    img);
	p.restore();
}

/** A team's characters (each player's first, for teams), side by side, faded to the left */
void drawCharacters(QPainter &p, const QRectF &container, const json &team, bool singles)
{
	std::vector<const json *> shown;
	for (const json &player : at(team, "player")) {
		size_t taken = 0;
		for (const json &character : at(player, "character")) {
			if (!singles && taken >= 1)
				break;
			if (!at(character, "assets").empty() && !str(character, "codename").empty()) {
				const json *asset = biggestAsset(character);
				if (asset && str(*asset, "asset").find(".webm") == std::string::npos)
					shown.push_back(asset);
				taken++;
			}
		}
	}
	if (shown.empty())
		return;
	layer(p, 1, [&](QPainter &q) {
		double w = container.width() / shown.size();
		for (size_t i = 0; i < shown.size(); i++)
			drawCharacter(q, QRectF(container.x() + w * i, container.y(), w, container.height()), container,
				      *shown[i]);
		QLinearGradient mask(container.topRight(), container.topLeft());
		mask.setColorAt(0, Qt::white);
		mask.setColorAt(0.7, Qt::white);
		mask.setColorAt(1, Qt::transparent);
		q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		q.fillRect(container, mask);
	});
}

/** HDBracket.ScoreText() */
QString scoreText(const json &set, int slot)
{
	const json &score = at(set, "score");
	if (!score.is_array() || score.size() <= static_cast<size_t>(slot) || score[slot].is_null())
		return {};
	double value = number(&score[slot]);
	if (value == -1)
		return QStringLiteral("DQ");
	bool started = truthy(get(set, "completed"));
	for (const json &s : score)
		started = started || number(&s) > 0;
	return started ? qs(text(&score[slot])) : QString();
}

/** One of a set's two rows: HDBracket.UpdateSet() and index.css */
void drawPlayer(QPainter &p, const Scene &scene, const json &set, int n, const QRectF &row, double left)
{
	const Theme &t = scene.theme;
	static const json none;
	const json &seats = at(set, "players");
	const json &slot = seats.is_array() && seats.size() > static_cast<size_t>(n) ? seats[n] : none;
	std::string id = str(slot, "id");
	const json &team = id.empty() || id == "0" ? none : at(scene.players, id);
	const json &winner = at(set, "winner");
	bool won = winner.is_number() && winner.get<int>() == n;
	bool lost = winner.is_number() && winner.get<int>() == 1 - n;
	bool singles = team.is_object() && at(team, "player").is_object() && at(team, "player").size() == 1;
	const json &player = singles ? at(team, "player").front() : none;
	double radius = t.radius / 2;

	layer(
		p, 1,
		[&](QPainter &q) {
			// The row is a .container too
			q.setPen(Qt::NoPen);
			q.setBrush(qcolor(t.background));
			q.drawRoundedRect(row, radius, radius);
			QPainterPath clip;
			clip.addRoundedRect(row, radius, radius);
			q.setClipPath(clip, Qt::IntersectClip);
			if (t.character && !team.is_null())
				drawCharacters(q,
					       QRectF(row.x() + row.width() * 0.45, row.y(), row.width() * 0.55, kRowH),
					       team, singles);

			double x = row.x() + left, right = row.right();
			for (const char *key : {"avatar", "online_avatar"}) {
				std::string path = str(player, key);
				if (!t.avatar || path.empty())
					continue;
				QRectF icon(x, row.y(), kRowH, kRowH);
				q.fillRect(icon, QColor(0x3f, 0x3f, 0x46));
				drawImage(q, icon, image(path), true);
				x += kRowH;
			}

			// The score, then the flags and the sponsor, from the right
			QRectF score(right - kScoreW, row.y(), kScoreW, kRowH);
			q.fillRect(score, won ? QBrush(qcolor(t.winner)) : brush(t.score, score));
			drawText(q, t, score, scoreText(set, n), font(t, 28), qcolor(won ? t.onWinner : t.scoreText),
				 Qt::AlignHCenter);
			std::vector<std::string> flags;
			if (t.countryFlag)
				flags.push_back(str(player, "country.asset"));
			if (t.stateFlag)
				flags.push_back(str(player, "state.asset"));
			double fx = score.left() - 4;
			for (size_t i = flags.size(); i-- > 0;) {
				if (flags[i].empty()) {
					fx -= i ? 3 : 0;
					continue;
				}
				fx -= kFlagW;
				drawImage(q, QRectF(fx, row.center().y() - kFlagH / 2, kFlagW, kFlagH), image(flags[i]),
					  true, 3);
				fx -= i ? 3 : 0;
			}
			fx -= 4 + kFlagW;
			if (std::string sponsor = str(player, "sponsor_logo"); !sponsor.empty()) {
				QRectF icon(fx, row.center().y() - kFlagW / 2, kFlagW, kFlagW);
				QPainterPath round;
				round.addRoundedRect(icon, t.radius, t.radius);
				q.fillPath(round, brush(t.sponsor, icon));
				drawImage(q, icon, image(sponsor), false, t.radius);
			}

			std::string seed = str(slot, "seed");
			if (t.seed && !team.is_null() && !seed.empty() && seed != "0") {
				QFont f = font(t, 18);
				double w = std::max(kRowH * 0.9, QFontMetricsF(f).horizontalAdvance(qs(seed)));
				QColor c = qcolor(t.text);
				c.setAlphaF(c.alphaF() * 0.7);
				drawText(q, t, QRectF(x, row.y(), w, kRowH), qs(seed), f, c, Qt::AlignHCenter);
				x += w;
			}

			// The name: the player with their sponsor, the team, or where they come from
			QRectF name(x, row.y(), std::max(0.0, fx - x), kRowH);
			std::vector<Run> runs;
			QFont f = font(t, 24);
			if (singles && !str(player, "name").empty()) {
				if (!str(player, "team").empty())
					runs.push_back({qs(str(player, "team")), f, qcolor(t.accent)});
				runs.push_back({qs(str(player, "name")), f, qcolor(t.text)});
			} else if (!team.is_null()) {
				std::string teamName = str(team, "name");
				if (teamName.empty())
					for (const json &member : at(team, "player"))
						if (!str(member, "name").empty())
							teamName +=
								(teamName.empty() ? "" : " / ") + str(member, "name");
				runs.push_back({qs(teamName), f, qcolor(t.text)});
			} else if (t.pending && truthy(get(slot, "pending")) && !str(slot, "source").empty()) {
				QColor c = qcolor(t.text);
				c.setAlphaF(c.alphaF() * 0.6);
				runs.push_back({qs(str(slot, "source")), font(t, 24, false, true), c});
			}
			drawRuns(q, t, name.adjusted(10, 0, -10, 0), runs);
			// The player whose run is in focus
			if (!scene.focusPlayer.empty() && id == scene.focusPlayer)
				q.fillRect(QRectF(name.x(), row.y(), 6, kRowH), qcolor(scene.focusColor));
		},
		[&](QImage &image) {
			if (lost && t.dimLosers)
				brightness(image, 0.6);
		});
}

/** A rounded box with the bracket's shadow, filled with the background */
void card(QPainter &p, const Theme &t, const QRectF &r)
{
	shadow(p, r, t.radius / 2);
	p.setPen(Qt::NoPen);
	p.setBrush(qcolor(t.background));
	p.drawRoundedRect(r, t.radius / 2, t.radius / 2);
}

} // namespace

// ---------------------------------------------------------------------
// Assets

Assets::Assets() : network_(new QNetworkAccessManager(this)) {}

Assets *Assets::instance()
{
	return assets;
}

void Assets::create()
{
	if (!assets)
		assets = new Assets();
}

void Assets::destroy()
{
	delete assets;
	assets = nullptr;
}

QImage Assets::image(const std::string &path, QSize *natural)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto [it, added] = images_.try_emplace(path);
	Entry &entry = it->second;
	if (added)
		QMetaObject::invokeMethod(this, [this, path]() { fetch(path, false); }, Qt::QueuedConnection);
	// Loading, or HyperDrive couldn't be reached (fetched again once it can)
	if (!entry.done || entry.failed) {
		missing = true;
		return {};
	}
	if (!entry.decoded) {
		// No bigger than any zoom needs: character art can be huge
		QBuffer buffer(&entry.data);
		QImageReader reader(&buffer);
		entry.natural = reader.size();
		if (entry.natural.width() > kMaxImage || entry.natural.height() > kMaxImage)
			reader.setScaledSize(entry.natural.scaled(kMaxImage, kMaxImage, Qt::KeepAspectRatio));
		entry.image = reader.read();
		// Flags are SVGs
		if (QSvgRenderer svg(entry.data); entry.image.isNull() && svg.isValid()) {
			entry.natural = svg.defaultSize();
			entry.image = QImage(entry.natural.scaled(256, 256, Qt::KeepAspectRatio),
					     QImage::Format_ARGB32_Premultiplied);
			entry.image.fill(Qt::transparent);
			QPainter p(&entry.image);
			svg.render(&p);
		}
		entry.image = entry.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
		if (!entry.natural.isValid())
			entry.natural = entry.image.size();
		entry.data.clear();
		entry.decoded = true;
	}
	if (natural)
		*natural = entry.natural;
	return entry.image;
}

QStringList Assets::families(const std::string &themeFont)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (fontsPending_ < 0) {
		fontsPending_ = static_cast<int>(std::size(kFonts));
		for (const char *path : kFonts)
			QMetaObject::invokeMethod(this, [this, path]() { fetch(path, true); }, Qt::QueuedConnection);
	}
	if (fontsPending_ > 0 || fontsFailed_)
		missing = true;
	QStringList list = fontFamilies_;
	if (!themeFont.empty())
		list.prepend(qs(themeFont));
	return list << QStringLiteral("Saira Condensed") << QStringLiteral("Noto Sans");
}

void Assets::clear()
{
	std::lock_guard<std::mutex> lock(mutex_);
	images_.clear();
	version_++;
}

void Assets::retry()
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto it = images_.begin(); it != images_.end();)
		it = it->second.failed ? images_.erase(it) : std::next(it);
	if (fontsFailed_) {
		fontsFailed_ = false;
		fontsPending_ = -1;
	}
	version_++;
}

void Assets::fetch(const std::string &path, bool isFont)
{
	QString url = qs(path);
	if (!url.startsWith(QStringLiteral("http://")) && !url.startsWith(QStringLiteral("https://"))) {
		Companion *companion = Companion::instance();
		if (!companion)
			return;
		const GlobalSettings &settings = companion->settings();
		std::string encoded;
		size_t start = 0;
		for (size_t slash; (slash = path.find('/', start)) != std::string::npos; start = slash + 1)
			encoded += hd::urlEncode(path.substr(start, slash - start)) + "/";
		encoded += hd::urlEncode(path.substr(start));
		url = qs(hd::baseUrl(hd::connectAddress(hd::parseAddress(settings.host, settings.port))) + "/" +
			 encoded);
	}
	QNetworkReply *reply = network_->get(QNetworkRequest(QUrl(url)));
	connect(reply, &QNetworkReply::finished, this, [this, reply, path, isFont]() {
		reply->deleteLater();
		QByteArray data = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
		// No answer at all (not a 404): asked again when HyperDrive is back
		bool failed = !reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid();
		std::lock_guard<std::mutex> lock(mutex_);
		if (isFont) {
			int id = data.isEmpty() ? -1 : QFontDatabase::addApplicationFontFromData(data);
			for (const QString &family : QFontDatabase::applicationFontFamilies(id))
				if (!fontFamilies_.contains(family))
					fontFamilies_ << family;
			fontsPending_ = std::max(0, fontsPending_ - 1);
			fontsFailed_ = fontsFailed_ || failed;
		} else if (auto it = images_.find(path); it != images_.end()) {
			it->second.done = true;
			it->second.failed = failed;
			it->second.data = data;
		}
		version_++;
	});
}

// ---------------------------------------------------------------------
// Pieces

Sprite paintSet(const Scene &scene, const std::string &id, const Box &box, double scale)
{
	const json &set = at(at(scene.bracket, "sets"), id);
	const Theme &t = scene.theme;
	return paint(grow(box, 8), scale, [&](QPainter &p) {
		QRectF r = rectOf(box);
		card(p, t, r);
		QPainterPath clip;
		clip.addRoundedRect(r, t.radius / 2, t.radius / 2);
		p.setClipPath(clip);
		double left = t.identifier ? kIdentifierW : 0;
		for (int n = 0; n < 2; n++)
			drawPlayer(p, scene, set, n, QRectF(r.x(), r.y() + n * (kRowH + kRowGap), r.width(), kRowH),
				   left);
		if (t.identifier) {
			QRectF identifier(r.x(), r.y(), kIdentifierW, r.height());
			p.fillRect(identifier, brush(t.score, identifier));
			drawText(p, t, identifier, qs(str(set, "identifier")), font(t, 16), qcolor(t.scoreText),
				 Qt::AlignHCenter);
		}
	});
}

Sprite paintHeader(const Scene &scene, const std::string &key, const Box &box, double scale)
{
	const Theme &t = scene.theme;
	QString name;
	for (const json &columns : at(scene.bracket, "sides"))
		for (const json &column : columns)
			if (str(column, "key") == key)
				name = qs(str(column, "name"));
	return paint(grow(box, 8), scale, [&](QPainter &p) {
		if (!t.roundNames)
			return;
		QRectF r = rectOf(box);
		card(p, t, r);
		drawText(p, t, r.adjusted(12, 2, -12, -2), name, font(t, 26), qcolor(t.text), Qt::AlignHCenter);
	});
}

Sprite paintByes(const Scene &scene, const std::string &key, const Box &box, double scale)
{
	QString names;
	for (const json &p : at(at(scene.bracket, "byes"), key))
		if (!str(p, "name").empty())
			names += (names.isEmpty() ? "" : ", ") + qs(str(p, "name"));
	Box area{box.x, box.y, box.w, 30};
	return paint(grow(area, 6), scale, [&](QPainter &p) {
		if (names.isEmpty())
			return;
		QRectF r = rectOf(area);
		QFont f = font(scene.theme, 20);
		QString text = QStringLiteral("Bye: ") + names;
		drawText(p, scene.theme, r.translated(0, 2), text, f, QColor(0, 0, 0, 40));
		drawText(p, scene.theme, r, text, f, qcolor(scene.theme.text));
	});
}

Sprite paintStandings(const Scene &scene, const Box &box, double scale)
{
	const Theme &t = scene.theme;
	const json &rows = at(scene.bracket, "standings");
	if (!rows.is_array() || rows.empty())
		return paint({box.x, box.y, 1, 1}, scale, [](QPainter &) {});
	bool swiss = str(scene.bracket, "type") == "SWISS", draws = false;
	for (const json &row : rows)
		draws = draws || number(get(row, "draws")) > 0;

	// The columns: rank, name, then the record, points, game difference and Buchholz
	QFont cell = font(t, 34), head = font(t, 34 * 0.8);
	head.setCapitalization(QFont::AllUppercase);
	std::vector<std::pair<QString, std::function<QString(const json &)>>> columns;
	columns.push_back({QStringLiteral("#"), [](const json &r) {
				   return qs(str(r, "rank"));
			   }});
	columns.push_back({QStringLiteral("Player"), {}});
	columns.push_back({draws ? QStringLiteral("W-L-D") : QStringLiteral("W-L"), [](const json &r) {
				   QString s = qs(str(r, "wins")) + "-" + qs(str(r, "losses"));
				   return number(get(r, "draws")) > 0 ? s + "-" + qs(str(r, "draws")) : s;
			   }});
	if (t.points)
		columns.push_back({QStringLiteral("Pts"), [](const json &r) {
					   return qs(str(r, "points"));
				   }});
	if (t.gameDiff)
		columns.push_back({QStringLiteral("+/-"), [](const json &r) {
					   double d = number(get(r, "gameDiff"));
					   return (d > 0 ? "+" : "") + qs(str(r, "gameDiff"));
				   }});
	if (swiss && t.buchholz)
		columns.push_back({QStringLiteral("Bh"), [](const json &r) {
					   return qs(str(r, "buchholz"));
				   }});

	// Table cells have the font's own line height
	double rowH = QFontMetricsF(cell).height() + 12, headH = QFontMetricsF(head).height() + 4;
	std::vector<double> widths;
	double fixed = 0;
	for (const auto &[title, value] : columns) {
		double w = QFontMetricsF(head).horizontalAdvance(title) + 16;
		if (value) {
			for (const json &row : rows)
				w = std::max(w, QFontMetricsF(cell).horizontalAdvance(value(row)) + 20);
			w = std::max(w, 34 * 1.5 + 20);
		}
		widths.push_back(value ? w : 0);
		fixed += value ? w : 0;
	}
	widths[1] = std::max(120.0, box.w - fixed);
	double height = 4 + headH + rows.size() * (rowH + 4);
	Box area{box.x, box.y, box.w, height};

	Sprite sprite = paint(grow(area, 8), scale, [&](QPainter &p) {
		double x = box.x, y = box.y + 4;
		QColor faded = qcolor(t.text);
		faded.setAlphaF(faded.alphaF() * 0.8);
		for (size_t c = 0; c < columns.size(); c++) {
			drawText(p, t, QRectF(x + 8, y, widths[c] - 16, headH), columns[c].first, head, faded,
				 c == 1 ? Qt::AlignLeft : Qt::AlignHCenter);
			x += widths[c];
		}
		y += headH + 4;
		double r = t.radius / 2;
		for (const json &row : rows) {
			bool top = t.highlightTop > 0 && number(get(row, "rank")) <= t.highlightTop;
			QPainterPath shape;
			shape.addRoundedRect(QRectF(box.x, y, box.w, rowH), r, r);
			shadow(p, QRectF(box.x, y, box.w, rowH), r);
			p.fillPath(shape, qcolor(t.chip));
			x = box.x;
			for (size_t c = 0; c < columns.size(); c++) {
				QRectF cellRect(x, y, widths[c], rowH);
				if (c == 0) {
					p.save();
					p.setClipPath(shape);
					p.fillRect(cellRect, top ? QBrush(qcolor(t.winner)) : brush(t.score, cellRect));
					p.restore();
					drawText(p, t, cellRect, columns[c].second(row), cell,
						 qcolor(top ? t.onWinner : t.scoreText), Qt::AlignHCenter);
				} else if (c == 1) {
					if (top)
						p.fillRect(QRectF(x, y, 4, rowH), qcolor(t.winner));
					double nx = x + 10;
					std::string playerId = str(row, "playerId");
					const json &team = at(scene.players, playerId);
					const json &players = at(team, "player");
					std::string name = str(row, "name");
					if (players.is_object() && players.size() == 1) {
						const json &player = players.front();
						for (const char *flag : {"country.asset", "state.asset"}) {
							QImage img = image(str(player, flag));
							if (img.isNull())
								continue;
							double fh = 34 * 0.8, fw = fh * img.width() / img.height();
							drawImage(p, QRectF(nx, y + (rowH - fh) / 2, fw, fh), img,
								  true);
							nx += fw + 6;
						}
						if (!str(player, "name").empty())
							name = str(player, "team").empty()
								       ? str(player, "name")
								       : str(player, "team") + " " +
										 str(player, "name");
					} else if (!str(team, "name").empty()) {
						name = str(team, "name");
					}
					drawText(p, t, QRectF(nx, y, x + widths[c] - nx - 10, rowH), qs(name), cell,
						 qcolor(t.chipText));
				} else {
					drawText(p, t, cellRect, columns[c].second(row), cell, qcolor(t.chipText),
						 Qt::AlignHCenter);
				}
				x += widths[c];
			}
			y += rowH + 4;
		}
	});
	sprite.at = grow(area, 8);
	return sprite;
}

Sprite paintLine(const Line &line, const Theme &theme, double scale, uint32_t outline)
{
	return paint(grow(line.bounds(), 12), scale, [&](QPainter &p) {
		QPainterPath path(QPointF(line.points[0][0], line.points[0][1]));
		for (size_t i = 1; i < line.points.size(); i++)
			path.lineTo(line.points[i][0], line.points[i][1]);
		if (line.arrow) {
			const auto &[x, y] = line.points.back();
			path.moveTo(x - 10, y - 8);
			path.lineTo(x, y);
			path.lineTo(x - 10, y + 8);
		}
		p.setBrush(Qt::NoBrush);
		if (outline) {
			p.setPen(QPen(qcolor(outline), 6, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
			p.drawPath(path);
			return;
		}
		p.setPen(QPen(QColor(0, 0, 0, 38), 9, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
		p.drawPath(path.translated(0, 2));
		p.setPen(QPen(qcolor(theme.lines), 5, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
		p.drawPath(path);
	});
}

Sprite paintRing(const Box &box, const Theme &theme, uint32_t color, double scale, bool bar)
{
	double radius = theme.radius / 2;
	return paint(grow(box, bar ? 2 : 36), scale, [&](QPainter &p) {
		QRectF r = rectOf(box);
		if (bar) {
			if (!theme.roundNames)
				return;
			QPainterPath clip;
			clip.addRoundedRect(r, radius, radius);
			p.setClipPath(clip);
			p.fillRect(QRectF(r.x(), r.bottom() - 4, r.width(), 4), qcolor(color));
			return;
		}
		// box-shadow: 0 0 0 4px, and 0 0 28px 4px: a ring, and a glow
		// around it fading out over 28px (a blur of 14px's deviation)
		QPainterPath inside;
		inside.addRoundedRect(r, radius, radius);
		p.setBrush(Qt::NoBrush);
		for (double d = 26; d >= 0; d -= 2) {
			QColor c = qcolor(color);
			c.setAlphaF(c.alphaF() * 0.5 * std::erfc((d + 1) / (14 * std::sqrt(2.0))));
			p.setPen(QPen(c, 2));
			// The innermost one under the ring, so there's no seam
			double by = 4 + d;
			p.drawRoundedRect(r.adjusted(-by, -by, by, by), radius + by, radius + by);
		}
		QPainterPath ring;
		ring.addRoundedRect(r.adjusted(-4, -4, 4, 4), radius + 4, radius + 4);
		p.fillPath(ring.subtracted(inside), qcolor(color));
	});
}

Sprite paintTitle(const json &info, const Theme &t, double width, double scale, const TitleStyle &style)
{
	auto textWidth = [&](const char *key, double size) {
		return str(info, key).empty() ? 0.0 : widthOf({{qs(str(info, key)), font(t, size), {}}});
	};
	double logoW = style.logo ? 120 : 0, w = width - 48;
	// The columns: as wide as their text, or the logo (120px) and two columns
	// (100% wide, 16px padding) shrunk to fit, in proportion to their widths,
	// as flex items are
	double col1 = std::max(textWidth("tournament", 24), textWidth("event", 20)),
	       col2 = std::max(textWidth("phase", 20), textWidth("group", 20));
	if (style.fit) {
		col1 = col1 > 0 ? col1 + 32 : 0;
		col2 = col2 > 0 ? col2 + 32 : 0;
		w = std::min(w, 16 + logoW + col1 + col2 + 16);
	} else {
		double shrink = (w + 80 + logoW) / (logoW + 2 * w);
		logoW -= logoW * shrink;
		col1 = col2 = w - w * shrink + 32;
	}
	double x = !style.fit || style.align == 0 ? 24 : style.align == 2 ? width - 24 - w : (width - w) / 2;
	QRectF bar(x, 16, w, kTitleHeight - 32);
	return paint({x, 0, w, kTitleHeight}, scale, [&](QPainter &p) {
		QColor background = qcolor(t.background);
		background.setAlphaF(background.alphaF() * style.opacity);
		p.setPen(Qt::NoPen);
		p.setBrush(background);
		p.drawRoundedRect(bar, t.radius / 2, t.radius / 2);
		QPainterPath clip;
		clip.addRoundedRect(bar, t.radius / 2, t.radius / 2);
		p.setClipPath(clip);
		QRectF logo(bar.x() + 16, bar.center().y() - 60, logoW, 120);
		if (style.logo)
			drawImage(p, logo, image("layout/logo.png"), false, t.radius);
		// Lines as high as their text (Saira's line height), empty ones none
		auto column = [&](double x, double colW, const char *a, double aSize, const char *b,
				  Qt::Alignment align) {
			double ha = str(info, a).empty() ? 0 : aSize, hb = str(info, b).empty() ? 0 : 20;
			QRectF r(x + 16, bar.center().y() - (ha + hb) / 2, colW - 32, ha);
			drawText(p, t, r, qs(str(info, a)), font(t, aSize), qcolor(t.text), align);
			drawText(p, t, QRectF(r.x(), r.bottom(), r.width(), hb), qs(str(info, b)), font(t, 20),
				 qcolor(t.text), align);
		};
		column(logo.right(), col1, "tournament", 24, "event", Qt::AlignLeft);
		column(logo.right() + col1, col2, "phase", 20, "group", style.fit ? Qt::AlignLeft : Qt::AlignRight);
	});
}

Sprite paintLabel(const std::string &label, const Theme &t, uint32_t color, double scale)
{
	QFont f = font(t, 30);
	QFontMetricsF m(f);
	double w = std::max(200.0, std::min(m.horizontalAdvance(qs(label)), 1600.0) + 56), h = 30 + 20;
	return paint({-w / 2, -h, w, h}, scale, [&](QPainter &p) {
		QRectF r(-w / 2, -h, w, h);
		QPainterPath shape;
		shape.addRoundedRect(r, t.radius / 2, t.radius / 2);
		p.fillPath(shape, qcolor(t.background));
		p.setClipPath(shape);
		p.fillRect(QRectF(r.x(), r.bottom() - 4, w, 4), qcolor(color));
		drawText(p, t, r.adjusted(28, 8, -28, -12), qs(label), f, qcolor(t.text), Qt::AlignHCenter);
	});
}

} // namespace hd::bracket
