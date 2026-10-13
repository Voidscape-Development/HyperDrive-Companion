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
#include "state.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace hd {

namespace {

bool isIndex(const std::string &key)
{
	return !key.empty() && key.size() < 10 &&
	       std::all_of(key.begin(), key.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

std::string keyText(const json &key)
{
	if (key.is_string())
		return key.get<std::string>();
	if (key.is_number_integer())
		return std::to_string(key.get<long long>());
	return key.dump();
}

/** The child of node at key, created as {} if it isn't an object or array */
json &child(json &node, const json &key)
{
	if (node.is_array() && key.is_number_integer()) {
		long long index = key.get<long long>();
		if (index >= 0 && index < 100000) {
			size_t i = static_cast<size_t>(index);
			while (node.size() <= i)
				node.push_back(nullptr);
			return node[i];
		}
	}
	if (!node.is_object())
		node = json::object();
	return node[keyText(key)];
}

std::string trimmed(const std::string &s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

std::vector<std::string> playerNames(const json *players)
{
	std::vector<std::string> names;
	for (int key : numericKeys(players)) {
		const json &player = (*players)[std::to_string(key)];
		std::string name = player.is_object() ? trimmed(text(get(player, "name"))) : "";
		if (!name.empty())
			names.push_back(name);
	}
	return names;
}

std::string join(const std::vector<std::string> &parts, const std::string &separator)
{
	std::string out;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i > 0)
			out += separator;
		out += parts[i];
	}
	return out;
}

std::string validColor(const json *value)
{
	std::string color = text(value);
	if (color.size() == 7 && color[0] == '#' && std::all_of(color.begin() + 1, color.end(), [](char c) {
		    return std::isxdigit(static_cast<unsigned char>(c));
	    }))
		return color;
	return "";
}

std::string teamDisplayName(const json *team)
{
	if (!team || !team->is_object())
		return "";
	std::string name = trimmed(text(get(*team, "teamName")));
	if (!name.empty())
		return name;
	return join(playerNames(get(*team, "player")), " / ");
}

} // namespace

bool applyDelta(json &state, const json &delta)
{
	if (!delta.is_object() || !delta.contains("path") || !delta["path"].is_array() || delta["path"].empty())
		return false;
	const json &path = delta["path"];
	std::string action =
		delta.contains("action") && delta["action"].is_string() ? delta["action"].get<std::string>() : "";

	json *node = &state;
	for (size_t i = 0; i + 1 < path.size(); i++) {
		json &next = child(*node, path[i]);
		if (!next.is_object() && !next.is_array())
			next = json::object();
		node = &next;
	}
	const json &last = path.back();
	if (action == "dictionary_item_removed" || action == "iterable_item_removed") {
		if (node->is_object()) {
			node->erase(keyText(last));
		} else if (node->is_array() && last.is_number_integer()) {
			// As `delete array[i]` in JavaScript: the indexes after it
			// stay, as later deltas expect
			long long index = last.get<long long>();
			if (index >= 0 && static_cast<size_t>(index) < node->size())
				(*node)[static_cast<size_t>(index)] = nullptr;
		}
		return true;
	}
	child(*node, last) = delta.contains("value") ? delta["value"] : json();
	return true;
}

bool applyDeltas(json &state, const json &deltas)
{
	if (!deltas.is_array())
		return false;
	for (const json &delta : deltas) {
		if (!applyDelta(state, delta))
			return false;
	}
	return true;
}

const json *get(const json &state, const std::string &path)
{
	const json *node = &state;
	size_t start = 0;
	while (start <= path.size()) {
		size_t dot = path.find('.', start);
		std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
		if (node->is_object()) {
			auto it = node->find(key);
			if (it == node->end())
				return nullptr;
			node = &*it;
		} else if (node->is_array() && isIndex(key)) {
			size_t index = static_cast<size_t>(std::stoul(key));
			if (index >= node->size())
				return nullptr;
			node = &(*node)[index];
		} else {
			return nullptr;
		}
		if (dot == std::string::npos)
			break;
		start = dot + 1;
	}
	return node;
}

std::string text(const json *value)
{
	if (!value)
		return "";
	if (value->is_string())
		return value->get<std::string>();
	if (value->is_number_integer())
		return std::to_string(value->get<long long>());
	if (value->is_number_unsigned())
		return std::to_string(value->get<unsigned long long>());
	if (value->is_number_float()) {
		double d = value->get<double>();
		if (std::floor(d) == d && std::fabs(d) < 1e15)
			return std::to_string(static_cast<long long>(d));
		return value->dump();
	}
	return "";
}

double number(const json *value, double fallback)
{
	if (!value)
		return fallback;
	if (value->is_number())
		return value->get<double>();
	if (value->is_string()) {
		const std::string &s = value->get_ref<const std::string &>();
		try {
			size_t used = 0;
			double d = std::stod(s, &used);
			if (used == s.size())
				return d;
		} catch (...) {
		}
	}
	return fallback;
}

bool truthy(const json *value)
{
	return value && value->is_boolean() && value->get<bool>();
}

std::vector<int> numericKeys(const json *object)
{
	std::vector<int> keys;
	if (!object || !object->is_object())
		return keys;
	for (auto it = object->begin(); it != object->end(); ++it) {
		if (isIndex(it.key()))
			keys.push_back(std::stoi(it.key()));
	}
	std::sort(keys.begin(), keys.end());
	return keys;
}

std::vector<int> scoreboardNumbers(const json &state)
{
	return numericKeys(get(state, "score"));
}

std::optional<TeamInfo> scoreboardTeam(const json &state, int scoreboard, int team)
{
	const json *data = get(state, "score." + std::to_string(scoreboard) + ".team." + std::to_string(team));
	if (!data || !data->is_object())
		return std::nullopt;
	TeamInfo info;
	info.players = playerNames(get(*data, "player"));
	info.name = teamDisplayName(data);
	info.score = static_cast<int>(number(get(*data, "score")));
	info.color = validColor(get(*data, "color"));
	info.losers = truthy(get(*data, "losers"));
	return info;
}

int bestOf(const json &state, int scoreboard)
{
	return static_cast<int>(number(get(state, "score." + std::to_string(scoreboard) + ".best_of")));
}

int winsNeeded(int bestOfValue)
{
	return bestOfValue > 0 ? bestOfValue / 2 + 1 : 0;
}

std::string matchName(const json &state, int scoreboard)
{
	return trimmed(text(get(state, "score." + std::to_string(scoreboard) + ".match")));
}

std::string setKey(const json &state, int scoreboard)
{
	std::string id = text(get(state, "score." + std::to_string(scoreboard) + ".set_id"));
	if (!id.empty())
		return "id:" + id;
	std::vector<std::string> names;
	for (int team = 1; team <= 2; team++) {
		auto info = scoreboardTeam(state, scoreboard, team);
		std::string name = info ? info->name : "";
		if (!name.empty())
			names.push_back(name);
	}
	if (names.empty())
		return "";
	std::sort(names.begin(), names.end());
	return "names:" + join(names, "\n");
}

std::optional<std::string> nextQueuedSet(const json &state, int scoreboard)
{
	const json *streams = get(state, "stream_queue.streams");
	if (!streams || !streams->is_array())
		return std::nullopt;
	for (const json &stream : *streams) {
		if (!stream.is_object() || static_cast<int>(number(get(stream, "scoreboard"), -1)) != scoreboard)
			continue;
		const json *sets = get(stream, "sets");
		if (!sets || !sets->is_array())
			return std::nullopt;
		for (const json &set : *sets) {
			if (!set.is_object() || truthy(get(set, "onStream")))
				continue;
			std::string names[2];
			for (int t = 1; t <= 2; t++) {
				names[t - 1] = teamDisplayName(get(set, "team." + std::to_string(t)));
				if (names[t - 1].empty())
					names[t - 1] = "?";
			}
			return names[0] + " vs " + names[1];
		}
		return std::nullopt;
	}
	return std::nullopt;
}

std::optional<BattleTeam> teamBattleTeam(const json &state, int team)
{
	const json *battle = get(state, "team_battle");
	if (!battle || !battle->is_object())
		return std::nullopt;
	const json *data = get(*battle, "team." + std::to_string(team));
	if (!data || !data->is_object())
		return std::nullopt;

	BattleTeam result;
	result.name = trimmed(text(get(*data, "sponsor")));
	result.color = validColor(get(*data, "color"));
	int activeNumber = static_cast<int>(number(get(*data, "active_player")));
	const json *players = get(*data, "player");
	int sum = 0;
	for (int n : numericKeys(players)) {
		const json &p = (*players)[std::to_string(n)];
		BattlePlayer player;
		player.number = n;
		player.name = p.is_object() ? text(get(p, "name")) : "";
		player.value = p.is_object() ? static_cast<int>(number(get(p, "dynamic_spinner"))) : 0;
		player.active = p.is_object() && truthy(get(p, "active"));
		player.dead = p.is_object() && truthy(get(p, "dead"));
		sum += player.value;
		result.players.push_back(player);
	}
	for (const BattlePlayer &p : result.players) {
		if (p.number == activeNumber)
			result.active = p.number;
	}
	if (result.active == 0) {
		for (const BattlePlayer &p : result.players) {
			if (p.active) {
				result.active = p.number;
				break;
			}
		}
	}
	result.total = static_cast<int>(
		number(get(*battle, "team" + std::to_string(team) + "_spinner-total"), static_cast<double>(sum)));
	result.firstTo = text(get(*battle, "battle_mode")) == "FIRST_TO";
	result.battleValue = static_cast<int>(number(get(*battle, "battle_value")));
	return result;
}

bool DisplayState::allShown() const
{
	for (const auto &entry : folders) {
		if (!entry.second)
			return false;
	}
	for (const auto &entry : groups) {
		if (!entry.second)
			return false;
	}
	return true;
}

DisplayState displayState(const json &state)
{
	DisplayState result;
	const json *display = get(state, "display");
	if (!display || !display->is_object())
		return result;
	result.known = true;
	for (const char *kind : {"folders", "groups"}) {
		const json *entries = get(*display, kind);
		if (!entries || !entries->is_object())
			continue;
		auto &target = std::string(kind) == "folders" ? result.folders : result.groups;
		for (auto it = entries->begin(); it != entries->end(); ++it)
			target[it.key()] = !(it.value().is_boolean() && !it.value().get<bool>());
	}
	return result;
}

std::optional<DisplayTarget> DisplayTarget::parse(const std::string &value)
{
	DisplayTarget target;
	if (value == "all")
		return target;
	size_t colon = value.find(':');
	if (colon == std::string::npos || colon + 1 >= value.size())
		return std::nullopt;
	std::string kind = value.substr(0, colon);
	if (kind == "folder")
		target.kind = Kind::Folder;
	else if (kind == "group")
		target.kind = Kind::Group;
	else
		return std::nullopt;
	target.name = value.substr(colon + 1);
	return target;
}

std::string DisplayTarget::toString() const
{
	switch (kind) {
	case Kind::Folder:
		return "folder:" + name;
	case Kind::Group:
		return "group:" + name;
	case Kind::All:
		break;
	}
	return "all";
}

StrikeInfo stageStrikeInfo(const json &state, int scoreboard)
{
	StrikeInfo info;
	const json *strike = get(state, "score." + std::to_string(scoreboard) + ".stage_strike");
	if (!strike || !strike->is_object())
		return info;
	info.stage = text(get(*strike, "selectedStage"));
	info.stageSelected = !info.stage.empty();
	info.game = static_cast<int>(number(get(*strike, "currGame")));

	bool striking = static_cast<int>(number(get(*strike, "currPlayer"), -1)) != -1;
	const json *striked = get(*strike, "strikedStages");
	if (striked && striked->is_array()) {
		for (const json &round : *striked) {
			if (round.is_array() && !round.empty())
				striking = true;
		}
	}
	info.active = striking && !info.stageSelected;
	return info;
}

std::vector<std::string> bracketChannels(const json &state)
{
	std::vector<std::string> channels{"main"};
	const json *data = get(state, "bracket.channels");
	if (data && data->is_object()) {
		for (auto it = data->begin(); it != data->end(); ++it) {
			if (it.key() != "main")
				channels.push_back(it.key());
		}
	}
	return channels;
}

std::string bracketFocusLabel(const json &state, const std::string &channel)
{
	const json *focus = get(state, "bracket.focus");
	if (!focus || !focus->is_object())
		return "";
	auto it = focus->find(channel);
	if (it == focus->end() || !it->is_object())
		return "";
	return text(get(*it, "label"));
}

} // namespace hd
