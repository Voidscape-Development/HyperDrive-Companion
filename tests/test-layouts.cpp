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
#include "test.hpp"

#include <filesystem>
#include <fstream>

#include "layouts.hpp"

using namespace hd;
namespace fs = std::filesystem;

TEST(layouts_parse)
{
	auto folders = parseLayouts(nlohmann::json::parse(R"({"layouts": [
		{"folder": "scoreboard", "pages": [
			{"name": "index", "path": "layout/scoreboard/index.html", "preview": "layout/scoreboard/index_preview.png"},
			{"name": "bad"}
		]},
		{"folder": "empty", "pages": []},
		"junk"
	]})"));
	CHECK_EQ(folders.size(), size_t(1));
	CHECK_EQ(folders[0].pages.size(), size_t(1));
	CHECK_EQ(folders[0].pages[0].preview, "layout/scoreboard/index_preview.png");
	CHECK(parseLayouts(nlohmann::json::array()).empty());
}

static void touch(const fs::path &path)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path).put('\n');
}

TEST(layouts_scan)
{
	fs::path root = fs::temp_directory_path() / "hyperdrive-companion-test-layouts";
	fs::remove_all(root);
	touch(root / "layout" / "top_8" / "index.html");
	touch(root / "layout" / "Scoreboard" / "index.html");
	touch(root / "layout" / "Scoreboard" / "index_preview.png");
	touch(root / "layout" / "Scoreboard" / "arms.HTML");
	touch(root / "layout" / "Scoreboard" / "index.js");
	touch(root / "layout" / "include" / "x.html");
	touch(root / "layout" / "css_only" / "a.css");

	auto folders = scanLayouts(root.u8string());
	CHECK_EQ(folders.size(), size_t(2));
	CHECK_EQ(folders[0].folder, "Scoreboard");
	CHECK_EQ(folders[0].pages.size(), size_t(2));
	CHECK_EQ(folders[0].pages[0].name, "arms");
	CHECK_EQ(folders[0].pages[0].preview, "");
	CHECK_EQ(folders[0].pages[1].path, "layout/Scoreboard/index.html");
	CHECK_EQ(folders[0].pages[1].preview, "layout/Scoreboard/index_preview.png");
	CHECK_EQ(folders[1].folder, "top_8");
	CHECK(scanLayouts((root / "missing").u8string()).empty());
	CHECK(scanLayouts("").empty());
	fs::remove_all(root);
}

TEST(layouts_url)
{
	LayoutOptions options;
	CHECK_EQ(layoutUrl("http://127.0.0.1:5500", "layout/scoreboard - MTF/index.html", options),
		 "http://127.0.0.1:5500/layout/scoreboard%20-%20MTF/index.html");
	options.scoreboard = 2;
	options.display = " Main , Top 8,";
	options.channel = "TV 1";
	options.extra = "?foo=bar";
	CHECK_EQ(layoutUrl("http://h:1", "layout/bracket/index.html", options),
		 "http://h:1/layout/bracket/index.html?scoreboardNumber=2&display=main,top%208&channel=TV%201&foo=bar");
	options = LayoutOptions();
	options.channel = "main";
	CHECK_EQ(layoutUrl("http://h:1", "layout/a/b.html", options), "http://h:1/layout/a/b.html");
}
