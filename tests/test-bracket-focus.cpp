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
#include <cmath>

#include "bracket-focus.hpp"
#include "test.hpp"

using namespace hd::bracket;
using json = nlohmann::json;

static json placementJson()
{
	return json::parse(R"({
		"sets": {"a": {"x": 40, "y": 96, "w": 380, "h": 82}, "b": {"x": 40, "y": 202, "w": 380, "h": 82},
		         "c": {"x": 520, "y": 149, "w": 380, "h": 82}},
		"headers": {"w:1": {"x": 40, "y": 40, "w": 380, "h": 44}, "w:2": {"x": 520, "y": 40, "w": 380, "h": 44}},
		"byes": {}, "standings": null,
		"lines": [{"from": "a", "to": "c", "points": [[420, 137], [470, 137], [470, 190], [520, 190]]},
		          {"from": "c", "to": null, "points": [[900, 190], [945, 190]], "arrow": true}],
		"width": 940, "height": 324
	})");
}

static json bracketJson()
{
	return json::parse(
		R"({"sides": {"winners": [{"key": "w:1", "sets": ["a", "b"]}, {"key": "w:2", "sets": ["c"]}]}})");
}

TEST(placement_parses)
{
	json value = placementJson();
	auto placed = parsePlacement(&value);
	CHECK(placed.has_value());
	CHECK_EQ(placed->sets.size(), 3u);
	CHECK(placed->sets["c"] == (Box{520, 149, 380, 82}));
	CHECK_EQ(placed->lines.size(), 2u);
	CHECK_EQ(placed->lines[1].to, std::string());
	CHECK(placed->lines[1].arrow);
	CHECK(placed->lines[0].bounds() == (Box{420, 137, 100, 53}));
	CHECK(!placed->standings.has_value());
	CHECK_EQ(placed->width, 940.0);

	json none = nullptr;
	CHECK(!parsePlacement(&none).has_value());
	CHECK(!parsePlacement(nullptr).has_value());
}

TEST(colors)
{
	CHECK_EQ(parseColor("#ffb800", 0), 0xffffb800u);
	CHECK_EQ(parseColor("#80ff0000", 0), 0x80ff0000u);
	CHECK_EQ(parseColor("rgba(18, 17, 28, 0.5)", 0), 0x8012111cu);
	CHECK_EQ(parseColor("rgb(1,2,3)", 0), 0xff010203u);
	CHECK_EQ(parseColor("transparent", 1), 0u);
	CHECK_EQ(parseColor("nope", 7), 7u);
	CHECK_EQ(textOn(0xffffb800), 0xff111214u);
	CHECK_EQ(textOn(0xff12111c), 0xffffffffu);
}

TEST(theme_defaults_and_values)
{
	Theme defaults = parseTheme(nullptr);
	CHECK_EQ(defaults.maxZoom, 2.0);
	CHECK_EQ(defaults.background, 0xff12111cu);

	json theme = json::parse(R"({"values": {
		"general": {"apply_colors": false, "text_color": "#ff0000", "font_family": "Oswald", "text_case": "uppercase"},
		"animation": {"enabled": true, "speed": 200},
		"bracket": {"seed_display": false, "identifier_display": true, "focus_dim": 50, "focus_max_zoom": 150,
		            "focus_time": 500, "winner_color": "#000000", "focus_glow_side": false,
		            "focus_glow_custom_player": true, "focus_glow_color_player": "#00ff00"}
	}})");
	Theme t = parseTheme(&theme);
	CHECK(!t.seed);
	CHECK(t.identifier);
	CHECK(t.avatar);
	CHECK(t.upper);
	CHECK_EQ(t.font, std::string("Oswald"));
	// Without "use this theme's colors", main.css's
	CHECK_EQ(t.text, 0xffffffffu);
	CHECK_EQ(t.focusDim, 0.5);
	CHECK_EQ(t.maxZoom, 1.5);
	CHECK_EQ(t.focusTime, 0.5);
	CHECK_EQ(t.speed, 2.0);
	CHECK_EQ(t.onWinner, 0xffffffffu);
	CHECK(!t.glows("side"));
	CHECK(t.glows("sets"));
	CHECK(t.glows(""));
	CHECK_EQ(t.glowColor("player"), 0xff00ff00u);
	CHECK_EQ(t.glowColor("sets"), t.focus);

	theme["values"]["general"]["apply_colors"] = true;
	theme["values"]["animation"]["enabled"] = false;
	theme["values"]["bracket"]["score_background"] = {{"color", "#112233"}, {"gradient", false}};
	t = parseTheme(&theme);
	CHECK_EQ(t.text, 0xffff0000u);
	CHECK_EQ(t.score.color, 0xff112233u);
	CHECK_EQ(t.speed, 0.0);
}

TEST(focus_sets_rounds_and_box)
{
	json p = placementJson();
	Placement placed = *parsePlacement(&p);
	json bracket = bracketJson();
	Theme theme;
	Box world{0, 0, placed.width, placed.height};

	Focus none = computeFocus(nullptr, &bracket, placed, theme, world);
	CHECK(!none.active);
	CHECK(none.box == world);
	CHECK(none.rounds.empty());

	json focus = json::parse(R"({"mode": "sets", "sets": ["a", "c", "gone"], "rounds": ["w:2"], "label": "Hi"})");
	Focus f = computeFocus(&focus, &bracket, placed, theme, world);
	CHECK(f.active);
	CHECK_EQ(f.sets.size(), 2u);
	CHECK_EQ(f.label, std::string("Hi"));
	// w:1 has a, w:2 is in focus
	CHECK_EQ(f.rounds.size(), 2u);
	CHECK(f.lineFocused(placed.lines[0]));
	CHECK(f.lineFocused(placed.lines[1]));
	// a, c and w:2's name
	CHECK(f.box == (Box{40, 40, 860, 191}));

	json only = json::parse(R"({"mode": "rounds", "sets": ["b"]})");
	Focus g = computeFocus(&only, &bracket, placed, theme, world);
	CHECK(!g.lineFocused(placed.lines[0]));
	CHECK(g.box == placed.sets["b"]);
}

TEST(camera)
{
	View v = fitView({0, 0, 1000, 500}, 1920, 1080, 0, 2);
	CHECK(std::abs(v.scale - 1.8) < 1e-9);
	CHECK_EQ(v.cx, 500.0);
	CHECK_EQ(v.ax, 960.0);
	// Capped at the max zoom
	CHECK_EQ(fitView({0, 0, 100, 50}, 1920, 1080, kLabelRoom, 2).scale, 2.0);
	Box shown = v.shows(1920, 1080);
	CHECK(std::abs(shown.x + shown.w / 2 - 500) < 1e-9);

	View a{0, 0, 0, 0, 1}, b{100, 100, 0, 0, 4};
	CHECK(std::abs(between(a, b, 0.5).scale - 2) < 1e-9);
	CHECK(std::abs(between(a, b, 1).cx - 100) < 1e-9);
	CHECK_EQ(easeInOut(0.5), 0.5);
	CHECK_EQ(easeOut(1.0), 1.0);
	CHECK(std::abs(easeSine(0.5) - 0.5) < 1e-9);
}
