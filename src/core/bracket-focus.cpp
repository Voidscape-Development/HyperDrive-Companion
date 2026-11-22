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
#include "bracket-focus.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "state.hpp"

namespace hd::bracket {

namespace {

const char *kGlowModes[] = {"sets", "rounds", "side", "player", "follow", "tour"};

Box boxFrom(const json &value)
{
	return {number(get(value, "x")), number(get(value, "y")), number(get(value, "w")), number(get(value, "h"))};
}

std::map<std::string, Box> boxesFrom(const json *value)
{
	std::map<std::string, Box> boxes;
	if (value && value->is_object())
		for (auto it = value->begin(); it != value->end(); ++it)
			boxes[it.key()] = boxFrom(*it);
	return boxes;
}

Fill fillFrom(const json *value, Fill fallback)
{
	if (value && value->is_string())
		return {parseColor(value->get<std::string>(), fallback.color)};
	if (!value || !value->is_object())
		return fallback;
	Fill fill;
	fill.color = parseColor(text(get(*value, "color")), fallback.color);
	fill.gradient = truthy(get(*value, "gradient"));
	fill.color2 = parseColor(text(get(*value, "color2")), fill.color);
	fill.direction = text(get(*value, "direction"));
	fill.position = number(get(*value, "position"), 50);
	return fill;
}

} // namespace

Box unite(const Box &a, const Box &b)
{
	double x = std::min(a.x, b.x), y = std::min(a.y, b.y);
	return {x, y, std::max(a.x + a.w, b.x + b.w) - x, std::max(a.y + a.h, b.y + b.h) - y};
}

bool overlaps(const Box &a, const Box &b)
{
	return a.x + a.w > b.x && a.x < b.x + b.w && a.y + a.h > b.y && a.y < b.y + b.h;
}

Box Line::bounds() const
{
	Box box;
	for (size_t i = 0; i < points.size(); i++) {
		Box point{points[i][0], points[i][1], 0, 0};
		box = i ? unite(box, point) : point;
	}
	return box;
}

std::optional<Placement> parsePlacement(const json *placement)
{
	if (!placement || !placement->is_object() || !placement->contains("sets"))
		return std::nullopt;
	Placement placed;
	placed.sets = boxesFrom(get(*placement, "sets"));
	placed.headers = boxesFrom(get(*placement, "headers"));
	placed.byes = boxesFrom(get(*placement, "byes"));
	if (const json *standings = get(*placement, "standings"); standings && standings->is_object())
		placed.standings = boxFrom(*standings);
	if (const json *lines = get(*placement, "lines"); lines && lines->is_array()) {
		for (const json &l : *lines) {
			Line line{text(get(l, "from")), text(get(l, "to")), {}, truthy(get(l, "arrow"))};
			if (const json *points = get(l, "points"); points && points->is_array())
				for (const json &p : *points)
					if (p.is_array() && p.size() == 2)
						line.points.push_back({number(&p[0]), number(&p[1])});
			if (line.points.size() >= 2)
				placed.lines.push_back(line);
		}
	}
	placed.width = number(get(*placement, "width"));
	placed.height = number(get(*placement, "height"));
	return placed;
}

uint32_t parseColor(const std::string &color, uint32_t fallback)
{
	unsigned a = 255, r, g, b;
	double alpha = 1;
	if (color.size() == 7 && color[0] == '#' && std::sscanf(color.c_str() + 1, "%2x%2x%2x", &r, &g, &b) == 3)
		return 0xff000000u | r << 16 | g << 8 | b;
	if (color.size() == 9 && color[0] == '#' && std::sscanf(color.c_str() + 1, "%2x%2x%2x%2x", &a, &r, &g, &b) == 4)
		return a << 24 | r << 16 | g << 8 | b;
	if (color.rfind("rgb", 0) == 0) {
		size_t open = color.find('(');
		int n = open == std::string::npos
				? 0
				: std::sscanf(color.c_str() + open + 1, "%u ,%u ,%u ,%lf", &r, &g, &b, &alpha);
		if (n >= 3) {
			a = static_cast<unsigned>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255));
			return a << 24 | (r & 255) << 16 | (g & 255) << 8 | (b & 255);
		}
	}
	return color == "transparent" ? 0 : fallback;
}

uint32_t textOn(uint32_t color)
{
	auto channel = [color](int shift) {
		double c = ((color >> shift) & 255) / 255.0;
		return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
	};
	double luminance = 0.2126 * channel(16) + 0.7152 * channel(8) + 0.0722 * channel(0);
	return 1.05 / (luminance + 0.05) >= (luminance + 0.05) / 0.056 ? 0xffffffff : 0xff111214;
}

uint32_t Theme::glowColor(const std::string &mode) const
{
	auto it = glowColors.find(mode);
	return it == glowColors.end() ? focus : it->second;
}

Theme parseTheme(const json *layoutTheme)
{
	Theme theme;
	const json *values = layoutTheme ? get(*layoutTheme, "values") : nullptr;
	if (!values || !values->is_object())
		return theme;
	auto value = [values](const char *section, const char *field) {
		const json *s = get(*values, section);
		return s && s->is_object() && s->contains(field) ? &(*s)[field] : nullptr;
	};
	auto is = [&value](const char *section, const char *field, bool when) {
		const json *v = value(section, field);
		return v && v->is_boolean() && v->get<bool>() == when;
	};
	auto color = [&value](const char *section, const char *field, uint32_t fallback) {
		return parseColor(text(value(section, field)), fallback);
	};

	// The body classes the layouts get (see globals.js's THEME_CLASSES)
	theme.avatar = !is("bracket", "avatar_display", false);
	theme.character = !is("bracket", "character_display", false);
	theme.countryFlag = !is("bracket", "country_flag_display", false);
	theme.stateFlag = !is("bracket", "state_flag_display", false);
	theme.seed = !is("bracket", "seed_display", false);
	theme.roundNames = !is("bracket", "round_names_display", false);
	theme.identifier = is("bracket", "identifier_display", true);
	theme.pending = !is("bracket", "pending_display", false);
	theme.dimLosers = !is("bracket", "dim_losers", false);
	theme.focusLabel = !is("bracket", "focus_label_display", false);
	theme.gameDiff = !is("standings", "game_diff_display", false);
	theme.points = !is("standings", "points_display", false);
	theme.buchholz = is("standings", "buchholz_display", true);
	theme.highlightTop = static_cast<int>(number(value("standings", "highlight_top")));
	theme.upper = text(value("general", "text_case")) == "uppercase";
	theme.textOutline = !is("general", "text_outline", false);
	theme.font = text(value("general", "font_family"));

	// Always the theme's
	theme.winner = color("bracket", "winner_color", theme.winner);
	theme.onWinner = textOn(theme.winner);
	theme.focus = color("bracket", "focus_color", theme.focus);
	theme.focusDim = std::clamp(number(value("bracket", "focus_dim"), 30) / 100, 0.0, 1.0);
	double zoom = number(value("bracket", "focus_max_zoom"), 200);
	theme.maxZoom = (std::isfinite(zoom) && zoom > 0 ? zoom : 200) / 100;
	theme.focusTime = std::max(0.0, number(value("bracket", "focus_time"), 1000) / 1000);
	double speed = number(value("animation", "speed"), 100);
	theme.speed = is("animation", "enabled", false) ? 0 : (speed > 0 ? speed : 100) / 100;
	for (const char *mode : kGlowModes) {
		std::string m = mode;
		if (is("bracket", ("focus_glow_" + m).c_str(), false))
			theme.noGlow.insert(m);
		if (is("bracket", ("focus_glow_custom_" + m).c_str(), true))
			theme.glowColors[m] = color("bracket", ("focus_glow_color_" + m).c_str(), theme.focus);
	}

	// "Use this theme's colors": the rest, else main.css's
	if (is("general", "apply_colors", true)) {
		theme.text = color("general", "text_color", theme.text);
		if (const json *bg = value("general", "background"))
			theme.background = fillFrom(bg, {theme.background}).color;
		theme.radius = number(value("general", "border_radius"), theme.radius);
		theme.accent = color("general", "primary_color", theme.accent);
		theme.scoreText = color("teams", "team1_score_color", theme.scoreText);
		theme.score = fillFrom(value("bracket", "score_background"), theme.score);
		theme.sponsor = fillFrom(value("bracket", "sponsor_background"), theme.sponsor);
		theme.lines = color("bracket", "lines_color", theme.lines);
		theme.chip = fillFrom(value("chip", "background"), {theme.background}).color;
		theme.chipText = color("chip", "text_color", theme.text);
		return theme;
	}
	theme.chip = theme.background;
	return theme;
}

bool Focus::lineFocused(const Line &line) const
{
	return sets.count(line.from) && (line.to.empty() || sets.count(line.to));
}

Focus computeFocus(const json *focus, const json *bracket, const Placement &placed, const Theme &theme,
		   const Box &world)
{
	Focus f;
	std::set<std::string> rounds;
	if (focus && focus->is_object()) {
		f.mode = text(get(*focus, "mode"));
		f.label = text(get(*focus, "label"));
		f.player = text(get(*focus, "player"));
		if (const json *sets = get(*focus, "sets"); sets && sets->is_array())
			for (const json &id : *sets)
				if (placed.sets.count(text(&id)))
					f.sets.insert(text(&id));
		if (const json *keys = get(*focus, "rounds"); keys && keys->is_array())
			for (const json &key : *keys)
				rounds.insert(text(&key));
	}
	f.active = !f.sets.empty();
	f.glow = theme.glows(f.mode);
	f.color = theme.glowColor(f.mode);

	// A round is highlighted when it's in focus, or any of its sets is
	const json *sides = bracket ? get(*bracket, "sides") : nullptr;
	for (const auto &[key, box] : placed.headers) {
		bool in = rounds.count(key) > 0;
		if (sides && sides->is_object())
			for (const json &columns : *sides)
				for (const json &column : columns.is_array() ? columns : json::array())
					if (text(get(column, "key")) == key && column.contains("sets") &&
					    column["sets"].is_array())
						for (const json &id : column["sets"])
							in = in || f.sets.count(text(&id));
		if (in)
			f.rounds.insert(key);
	}

	std::vector<Box> boxes;
	for (const std::string &id : f.sets)
		boxes.push_back(placed.sets.at(id));
	for (const std::string &key : rounds)
		if (f.active && placed.headers.count(key))
			boxes.push_back(placed.headers.at(key));
	f.box = boxes.empty() ? world : boxes[0];
	for (const Box &b : boxes)
		f.box = unite(f.box, b);
	return f;
}

Box View::shows(double width, double height) const
{
	return {-left() / scale, -top() / scale, width / scale, height / scale};
}

View fitView(const Box &target, double width, double height, double labelRoom, double maxZoom)
{
	double scale =
		std::min({(width - kPadding * 2) / target.w, (height - kPadding * 2 - labelRoom) / target.h, maxZoom});
	return {target.x + target.w / 2, target.y + target.h / 2, width / 2, (height - labelRoom) / 2,
		std::max(scale, 1e-4)};
}

View between(const View &a, const View &b, double t)
{
	auto lerp = [](double p, double q, double f) {
		return p + (q - p) * f;
	};
	double ratio = b.scale / a.scale;
	double scale = a.scale * std::pow(ratio, t);
	// The center moves in step with 1 / scale, the size of the world on
	// screen, so the point that stays put does
	double u = std::abs(std::log(ratio)) < 0.01 ? t : (1 / scale - 1 / a.scale) / (1 / b.scale - 1 / a.scale);
	return {lerp(a.cx, b.cx, u), lerp(a.cy, b.cy, u), lerp(a.ax, b.ax, t), lerp(a.ay, b.ay, t), scale};
}

double easeInOut(double t)
{
	return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}

double easeOut(double t)
{
	return 1 - std::pow(1 - t, 3);
}

double easeSine(double t)
{
	return 0.5 - std::cos(t * 3.14159265358979) / 2;
}

} // namespace hd::bracket
