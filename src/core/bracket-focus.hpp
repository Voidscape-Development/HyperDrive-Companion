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
 * What the native bracket focus source shows, as HyperDrive's
 * layout/bracket_focus does: the bracket placed by HyperDrive
 * (bracket.channels.<channel>.placement), the layout theme's bracket options,
 * what's in focus on the channel (bracket.focus.<channel>) and the camera's
 * moves. No OBS or Qt here.
 */

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hd::bracket {

using json = nlohmann::json;

// Sizes in world pixels, as the layout's
constexpr double kPadding = 60;     // around what's in focus, on screen
constexpr double kLabelRoom = 90;   // above or under it, for the caption
constexpr double kLabelMargin = 32; // between the caption and the edge
constexpr double kTitleHeight = 100;
constexpr double kFadeTime = 0.6;

struct Box {
	double x = 0, y = 0, w = 0, h = 0;
	bool operator==(const Box &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};
Box unite(const Box &a, const Box &b);
bool overlaps(const Box &a, const Box &b);

struct Line {
	std::string from;
	/** "" for an arrow out of the phase */
	std::string to;
	std::vector<std::array<double, 2>> points;
	bool arrow = false;
	Box bounds() const;
};

struct Placement {
	std::map<std::string, Box> sets, headers, byes;
	std::optional<Box> standings;
	std::vector<Line> lines;
	double width = 0, height = 0;
};

/** bracket.channels.<channel>.placement, or nothing (none, or an older HyperDrive) */
std::optional<Placement> parsePlacement(const json *placement);

/** A color as 0xAARRGGBB: "#rrggbb", Qt's "#aarrggbb" or "rgb[a](r, g, b[, a])" */
uint32_t parseColor(const std::string &color, uint32_t fallback);
/** Dark or white text, whichever reads better on a color */
uint32_t textOn(uint32_t color);

struct Fill {
	Fill(uint32_t c = 0) : color(c), color2(c) {}
	uint32_t color;
	uint32_t color2;
	bool gradient = false;
	/** CSS: "to right", "135deg"... */
	std::string direction;
	double position = 50;
};

/** The layout theme's options the bracket focus layout uses (layout_theme.values) */
struct Theme {
	bool avatar = true, character = true, countryFlag = true, stateFlag = true, seed = true;
	bool roundNames = true, identifier = false, pending = true, dimLosers = true, focusLabel = true;
	/** text_case "uppercase" */
	bool upper = false;
	/** The text's faint outline (main.css's -webkit-text-stroke) */
	bool textOutline = true;
	/** The theme's font, or "" for the layout's */
	std::string font;
	uint32_t text = 0xffffffff, background = 0xff12111c, accent = 0xffffb800, scoreText = 0xffffffff;
	uint32_t winner = 0xffffb800, onWinner = 0xff111214, lines = 0xff000000, focus = 0xffffb800;
	/** The standings' cells */
	uint32_t chip = 0xff12111c, chipText = 0xffffffff;
	Fill score{0xff26272c}, sponsor;
	double radius = 12;
	/** Opacity of what's out of focus */
	double focusDim = 0.3;
	/** The most a set is enlarged (2 = 200%) */
	double maxZoom = 2;
	/** Seconds the camera takes to move */
	double focusTime = 1;
	/** Animation speed, 1 = normal; 0 = no animations */
	double speed = 1;
	int highlightTop = 0;
	bool gameDiff = true, points = true, buchholz = false;
	/** focus_glow_<mode> off, and focus_glow_color_<mode> with focus_glow_custom_<mode> on */
	std::set<std::string> noGlow;
	std::map<std::string, uint32_t> glowColors;

	bool glows(const std::string &mode) const { return !noGlow.count(mode); }
	uint32_t glowColor(const std::string &mode) const;
};

Theme parseTheme(const json *layoutTheme);

/** What's in focus on a channel */
struct Focus {
	std::string mode;
	/** The sets in focus that are placed */
	std::set<std::string> sets;
	/** Round names highlighted: in focus, or with a set in focus */
	std::set<std::string> rounds;
	/** Whether sets are in focus: the rest is dimmed */
	bool active = false;
	/** Outlined, with the theme's glow on for the mode */
	bool glow = false;
	uint32_t color = 0;
	std::string label;
	/** player mode: the player's slot */
	std::string player;
	/** What the camera shows, in world pixels */
	Box box;

	bool lineFocused(const Line &line) const;
};

/**
 * The focus (bracket.focus.<channel>, null for none) on a bracket
 * (bracket.channels.<channel>.bracket) placed in a world of `world`'s size.
 */
Focus computeFocus(const json *focus, const json *bracket, const Placement &placed, const Theme &theme,
		   const Box &world);

/** The camera: shows the world point (cx, cy) at (ax, ay) on screen, at a scale */
struct View {
	double cx = 0, cy = 0, ax = 0, ay = 0, scale = 1;
	/** Where it puts the world's corner */
	double left() const { return ax - cx * scale; }
	double top() const { return ay - cy * scale; }
	/** What it shows of the world, in a viewport */
	Box shows(double width, double height) const;
};

/** Where the focus caption goes: one of the 8 places along the edges */
struct CaptionPlace {
	/** 0 top, 1 middle, 2 bottom */
	int row = 2;
	/** 0 left, 1 center, 2 right */
	int column = 1;
	/** "top_left", "middle_right", "bottom_center"... (bottom center otherwise) */
	static CaptionPlace parse(const std::string &value);
};

/** Room the camera leaves free in the viewport, from each edge */
struct Room {
	double top = 0, bottom = 0, left = 0, right = 0;
};

/**
 * The room for a caption (as wide as width) at a place: above or under what's
 * in focus at the top or the bottom, beside it in the middle
 */
Room captionRoom(const CaptionPlace &place, double width);

/** Where a caption of a size goes in a viewport (page pixels): its bottom center */
std::array<double, 2> captionAt(const CaptionPlace &place, const Box &viewport, double width, double height);

/** The view of a box in a viewport, leaving room free */
View fitView(const Box &target, double width, double height, const Room &room, double maxZoom);
/**
 * The view t (0 to 1) of the way from a to b: the scale changes
 * geometrically, around the one point that stays put on screen, so zooming
 * in feels like zooming out played backwards.
 */
View between(const View &a, const View &b, double t);

double easeInOut(double t);
double easeOut(double t);
double easeSine(double t);

} // namespace hd::bracket
