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
#pragma once

/*
 * Paints the native bracket focus source's pieces as HyperDrive's
 * layout/bracket_focus draws them (its index.css, with the layout theme):
 * each set, round name, line and outline is a picture (a sprite) painted at
 * the scale the camera needs, on any thread, and the images and fonts it
 * shows come from HyperDrive's web server.
 */

#include <atomic>
#include <map>
#include <mutex>
#include <string>

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QStringList>

#include <nlohmann/json.hpp>

#include "core/bracket-focus.hpp"

class QNetworkAccessManager;

namespace hd::bracket {

/**
 * The images (players' avatars, flags, characters, the logo) and the
 * layouts' fonts, fetched once from HyperDrive's web server (or their URL)
 * for every bracket source. Lives on the UI thread; image() and families()
 * from any thread.
 */
/** The HyperDrive (its web server) the images of this thread's painting come from */
void setAssetBase(const std::string &base);

class Assets : public QObject {
	Q_OBJECT

public:
	static Assets *instance();
	static void create();
	static void destroy();

	/**
	 * A path on HyperDrive's server ("user_data/...") or a URL: its image,
	 * or a null one while it loads (the sprite is then painted again) or
	 * when there's none.
	 */
	QImage image(const std::string &path, QSize *natural = nullptr);
	/** The fonts to paint with: the theme's, then the layouts' */
	QStringList families(const std::string &themeFont);
	/** Changes when an image or the fonts arrive */
	uint64_t version() const { return version_.load(); }
	/** An address changed: everything is fetched again */
	void clear();
	/** HyperDrive is back: what couldn't be fetched is fetched again */
	void retry();

private:
	Assets();
	void fetch(const std::string &path, bool font);

	struct Entry {
		bool done = false;
		QByteArray data;
		QImage image;
		/** Its size before it was made smaller */
		QSize natural;
		bool decoded = false;
		/** HyperDrive couldn't be reached */
		bool failed = false;
	};
	QNetworkAccessManager *network_ = nullptr;
	std::mutex mutex_;
	std::map<std::string, Entry> images_;
	QStringList fontFamilies_;
	int fontsPending_ = -1;
	bool fontsFailed_ = false;
	std::atomic<uint64_t> version_{1};
};

struct Sprite {
	/** Premultiplied ARGB */
	QImage image;
	/** Where it goes, in world pixels */
	Box at;
	/** Painted while an image or the fonts were loading */
	bool incomplete = false;
};

/** What the pieces are painted from: the channel's bracket and players, and the theme */
struct Scene {
	nlohmann::json bracket;
	/** The channel's player list's slots (bracket.rosters.<key>.slot) */
	nlohmann::json players;
	Theme theme;
	/** The player whose run is in focus, and the focus color */
	std::string focusPlayer;
	uint32_t focusColor = 0;
};

Sprite paintSet(const Scene &scene, const std::string &id, const Box &box, double scale);
Sprite paintHeader(const Scene &scene, const std::string &key, const Box &box, double scale);
Sprite paintByes(const Scene &scene, const std::string &key, const Box &box, double scale);
/** The pool standings, from the top of box (as high as its rows) */
Sprite paintStandings(const Scene &scene, const Box &box, double scale);
/** A line (with its shadow), or its outline in a color */
Sprite paintLine(const Line &line, const Theme &theme, double scale, uint32_t outline = 0);
/** The bar under a round name in focus (the source draws the outlines of sets in focus) */
Sprite paintBar(const Box &box, const Theme &theme, uint32_t color, double scale);

struct TitleStyle {
	bool logo = true;
	/** As wide as what it shows, not the page */
	bool fit = false;
	/** When it fits: 0 left, 1 center, 2 right */
	int align = 0;
	/** Its background's */
	double opacity = 1;
};

/** The title bar: the logo, tournament, event, phase and phase group */
Sprite paintTitle(const nlohmann::json &info, const Theme &theme, double width, double scale,
		  const TitleStyle &style = {});
/** The caption saying what's in focus, its bottom center at (0, 0) */
Sprite paintLabel(const std::string &label, const Theme &theme, uint32_t color, double scale);

} // namespace hd::bracket
