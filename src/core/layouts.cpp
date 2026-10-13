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
#include "layouts.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <system_error>

#include "address.hpp"

namespace fs = std::filesystem;

namespace hd {

namespace {

// Folders in layout/ that hold layouts' shared files rather than a layout,
// and old layouts (as HyperDrive's GET /layouts)
const std::set<std::string> kSkipped = {"include", "fonts", "icons", "game_screenshots", "deprecated"};

std::string lower(std::string s)
{
	for (char &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

bool lessNoCase(const std::string &a, const std::string &b)
{
	return lower(a) < lower(b);
}

std::string stringOf(const nlohmann::json &object, const char *key)
{
	auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : "";
}

std::string encodePath(const std::string &path)
{
	std::string out;
	size_t start = 0;
	while (true) {
		size_t slash = path.find('/', start);
		out += urlEncode(path.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
		if (slash == std::string::npos)
			break;
		out += '/';
		start = slash + 1;
	}
	return out;
}

} // namespace

std::vector<LayoutFolder> parseLayouts(const nlohmann::json &json)
{
	std::vector<LayoutFolder> folders;
	if (!json.is_object() || !json.contains("layouts") || !json["layouts"].is_array())
		return folders;
	for (const nlohmann::json &item : json["layouts"]) {
		if (!item.is_object() || !item.contains("pages") || !item["pages"].is_array())
			continue;
		LayoutFolder folder;
		folder.folder = stringOf(item, "folder");
		for (const nlohmann::json &page : item["pages"]) {
			if (!page.is_object())
				continue;
			LayoutPage p{stringOf(page, "name"), stringOf(page, "path"), stringOf(page, "preview")};
			if (!p.name.empty() && !p.path.empty())
				folder.pages.push_back(p);
		}
		if (!folder.folder.empty() && !folder.pages.empty())
			folders.push_back(folder);
	}
	return folders;
}

std::vector<LayoutFolder> scanLayouts(const std::string &hyperdriveDir)
{
	std::vector<LayoutFolder> folders;
	if (hyperdriveDir.empty())
		return folders;
	std::error_code ec;
	fs::path layoutDir = fs::u8path(hyperdriveDir) / "layout";
	if (!fs::is_directory(layoutDir, ec))
		return folders;

	for (const fs::directory_entry &entry : fs::directory_iterator(layoutDir, ec)) {
		if (!entry.is_directory(ec))
			continue;
		std::string name = entry.path().filename().u8string();
		if (name.empty() || name[0] == '.' || kSkipped.count(name))
			continue;

		std::set<std::string> files;
		for (const fs::directory_entry &file : fs::directory_iterator(entry.path(), ec)) {
			if (file.is_regular_file(ec))
				files.insert(file.path().filename().u8string());
		}
		LayoutFolder folder;
		folder.folder = name;
		for (const std::string &file : files) {
			if (file.size() <= 5 || lower(file.substr(file.size() - 5)) != ".html")
				continue;
			std::string page = file.substr(0, file.size() - 5);
			std::string preview = page + "_preview.png";
			folder.pages.push_back({page, "layout/" + name + "/" + file,
						files.count(preview) ? "layout/" + name + "/" + preview : ""});
		}
		std::sort(folder.pages.begin(), folder.pages.end(),
			  [](const LayoutPage &a, const LayoutPage &b) { return lessNoCase(a.name, b.name); });
		if (!folder.pages.empty())
			folders.push_back(folder);
	}
	std::sort(folders.begin(), folders.end(),
		  [](const LayoutFolder &a, const LayoutFolder &b) { return lessNoCase(a.folder, b.folder); });
	return folders;
}

std::string layoutUrl(const std::string &baseUrl, const std::string &path, const LayoutOptions &options)
{
	std::string url = baseUrl + "/" + encodePath(path);
	std::vector<std::string> query;
	if (options.scoreboard > 1)
		query.push_back("scoreboardNumber=" + std::to_string(options.scoreboard));
	if (!options.display.empty()) {
		// Group names are lowercase, without spaces around the commas
		std::string groups = lower(options.display);
		std::string encoded;
		size_t start = 0;
		while (true) {
			size_t comma = groups.find(',', start);
			std::string group =
				groups.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
			size_t first = group.find_first_not_of(" \t");
			size_t last = group.find_last_not_of(" \t");
			group = first == std::string::npos ? "" : group.substr(first, last - first + 1);
			if (!group.empty())
				encoded += (encoded.empty() ? "" : ",") + urlEncode(group);
			if (comma == std::string::npos)
				break;
			start = comma + 1;
		}
		if (!encoded.empty())
			query.push_back("display=" + encoded);
	}
	if (!options.channel.empty() && options.channel != "main")
		query.push_back("channel=" + urlEncode(options.channel));
	std::string extra = options.extra;
	while (!extra.empty() && (extra[0] == '?' || extra[0] == '&'))
		extra.erase(0, 1);
	if (!extra.empty())
		query.push_back(extra);

	for (size_t i = 0; i < query.size(); i++)
		url += (i == 0 ? "?" : "&") + query[i];
	return url;
}

} // namespace hd
