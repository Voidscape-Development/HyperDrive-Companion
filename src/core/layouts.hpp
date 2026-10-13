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
 * The layouts a HyperDrive Layout source can show: from HyperDrive's
 * GET /layouts, or found in a HyperDrive folder for versions without it.
 */

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hd {

struct LayoutPage {
	/** "index" */
	std::string name;
	/** "layout/scoreboard/index.html", relative to HyperDrive's web server */
	std::string path;
	/** "layout/scoreboard/index_preview.png", or "" */
	std::string preview;
};

struct LayoutFolder {
	std::string folder;
	std::vector<LayoutPage> pages;
};

/** GET /layouts's answer */
std::vector<LayoutFolder> parseLayouts(const nlohmann::json &json);

/** The layouts in a HyperDrive folder (the one with layout/ in it) */
std::vector<LayoutFolder> scanLayouts(const std::string &hyperdriveDir);

struct LayoutOptions {
	/** ?scoreboardNumber=, left out for 1 */
	int scoreboard = 1;
	/** ?display=<group>[,<group>...] */
	std::string display;
	/** ?channel= (bracket layouts), left out for "main" */
	std::string channel;
	/** More of the query, as typed: "a=1&b=2" */
	std::string extra;
};

/** The page's URL on HyperDrive's web server, with its options */
std::string layoutUrl(const std::string &baseUrl, const std::string &path, const LayoutOptions &options);

} // namespace hd
