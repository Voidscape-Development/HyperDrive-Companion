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
#include "commands.hpp"

#include <cctype>

#include "address.hpp"

namespace hd {

namespace {

std::string sbPrefix(int sb)
{
	return "/scoreboard" + std::to_string(sb);
}

std::string team(int t)
{
	return "-team" + std::to_string(t);
}

} // namespace

bool commandSucceeded(int status, const std::string &body)
{
	if (status < 200 || status >= 300)
		return false;
	size_t start = body.find_first_not_of(" \t\r\n");
	if (start == std::string::npos || body.size() - start < 5)
		return true;
	std::string head = body.substr(start, 6);
	for (char &c : head)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	// "ERROR" as a word, not "ERRORS_FIXED"
	if (head.compare(0, 5, "ERROR") != 0)
		return true;
	return head.size() > 5 && (std::isalnum(static_cast<unsigned char>(head[5])) || head[5] == '_');
}

namespace scoreboard {

Request scoreUp(int sb, int t)
{
	return {sbPrefix(sb) + team(t) + "-scoreup"};
}

Request scoreDown(int sb, int t)
{
	return {sbPrefix(sb) + team(t) + "-scoredown"};
}

Request swapTeams(int sb)
{
	return {sbPrefix(sb) + "-swap-teams"};
}

Request teamColor(int sb, int t, const std::string &color)
{
	std::string hex = !color.empty() && color[0] == '#' ? color.substr(1) : color;
	return {sbPrefix(sb) + team(t) + "-color-" + hex};
}

Request reset(int sb, ResetKind kind)
{
	const char *suffix = "-reset-scores";
	switch (kind) {
	case ResetKind::Scores:
		break;
	case ResetKind::Match:
		suffix = "-reset-match";
		break;
	case ResetKind::Players:
		suffix = "-reset-players";
		break;
	case ResetKind::All:
		suffix = "-clear-all";
		break;
	}
	return {sbPrefix(sb) + suffix};
}

Request pullStreamSet(int sb)
{
	return {sbPrefix(sb) + "-pull-stream"};
}

Request loadNextQueued(int sb)
{
	return {"/stream-queue/load-next?scoreboard=" + std::to_string(sb)};
}

Request openSetSelector(int sb)
{
	return {sbPrefix(sb) + "-open-set"};
}

} // namespace scoreboard

namespace display {

const char *actionName(Action action)
{
	switch (action) {
	case Action::Show:
		return "show";
	case Action::Hide:
		return "hide";
	case Action::Toggle:
		break;
	}
	return "toggle";
}

Request set(const DisplayTarget &target, Action action)
{
	std::string name = actionName(action);
	switch (target.kind) {
	case DisplayTarget::Kind::Folder:
		return {"/display/folder/" + urlEncode(target.name) + "/" + name};
	case DisplayTarget::Kind::Group:
		return {"/display/group/" + urlEncode(target.name) + "/" + name};
	case DisplayTarget::Kind::All:
		break;
	}
	return {"/display/all/" + name};
}

} // namespace display

namespace teamBattle {

Request scoreUp(int t)
{
	return {"/team-battle" + team(t) + "-scoreup"};
}

Request scoreDown(int t)
{
	return {"/team-battle" + team(t) + "-scoredown"};
}

Request nextPlayer(int t)
{
	return {"/team-battle" + team(t) + "-next-player"};
}

Request setActive(int t, int player)
{
	return {"/team-battle" + team(t) + "-player" + std::to_string(player) + "-active"};
}

Request resetStocks()
{
	return {"/team-battle-reset-stocks"};
}

Request reset()
{
	return {"/team-battle-reset"};
}

} // namespace teamBattle

namespace stageStrike {

Request run(int sb, Command command, int t)
{
	nlohmann::json body = {{"scoreboardNumber", sb}};
	const char *path = "/stage_strike_undo";
	switch (command) {
	case Command::Undo:
		break;
	case Command::Redo:
		path = "/stage_strike_redo";
		break;
	case Command::Reset:
		path = "/stage_strike_reset";
		break;
	case Command::RpsWin:
		path = "/stage_strike_rps_win";
		// The stage strike's players are 0 (left) and 1 (right)
		body["winner"] = t - 1;
		break;
	case Command::GameWin:
		path = "/stage_strike_match_win";
		body["winner"] = t - 1;
		break;
	}
	return {path, "POST", body};
}

} // namespace stageStrike

namespace bracketFocus {

Request set(Mode mode, const std::string &channel, int sb, int tourInterval)
{
	std::string name = "all";
	std::string query;
	switch (mode) {
	case Mode::All:
		break;
	case Mode::NextRound:
		name = "next-round";
		break;
	case Mode::PreviousRound:
		name = "previous-round";
		break;
	case Mode::Follow:
		name = "follow";
		query = "scoreboard=" + std::to_string(sb);
		break;
	case Mode::Tour:
		name = "tour";
		if (tourInterval > 0)
			query = "interval=" + std::to_string(tourInterval);
		break;
	}
	if (!channel.empty() && channel != "main")
		query += (query.empty() ? "" : "&") + std::string("channel=") + urlEncode(channel);
	return {"/bracket-focus/" + name + (query.empty() ? "" : "?" + query)};
}

} // namespace bracketFocus

Request layouts()
{
	return {"/layouts"};
}

} // namespace hd
