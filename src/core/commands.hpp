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
 * The web server's links the plugin uses (see src/WebServer.py in
 * HyperDrive, or /api/docs on a running HyperDrive), as in the Stream Deck
 * plugin's commands.ts. Teams are 1 (left) or 2 (right).
 */

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "state.hpp"

namespace hd {

struct Request {
	Request(std::string path_, std::string method_ = "GET", std::optional<nlohmann::json> body_ = std::nullopt)
		: path(std::move(path_)),
		  method(std::move(method_)),
		  body(std::move(body_))
	{
	}

	std::string path;
	/** "GET" or "POST" */
	std::string method;
	/** Sent as JSON for a POST */
	std::optional<nlohmann::json> body;
};

/** HyperDrive answered, and didn't refuse it (some actions answer 200 with "ERROR ...") */
bool commandSucceeded(int status, const std::string &body);

namespace scoreboard {
Request scoreUp(int sb, int team);
Request scoreDown(int sb, int team);
Request swapTeams(int sb);
/** color: "#rrggbb" or "rrggbb" */
Request teamColor(int sb, int team, const std::string &color);

enum class ResetKind { Scores, Match, Players, All };
Request reset(int sb, ResetKind kind);

/** The set on stream from start.gg */
Request pullStreamSet(int sb);
/** The next set of the stream queue showing on this scoreboard */
Request loadNextQueued(int sb);
/** Opens the set selection window in HyperDrive */
Request openSetSelector(int sb);
} // namespace scoreboard

namespace display {
enum class Action { Show, Hide, Toggle };
Request set(const DisplayTarget &target, Action action);
const char *actionName(Action action);
} // namespace display

namespace teamBattle {
Request scoreUp(int team);
Request scoreDown(int team);
Request nextPlayer(int team);
/** player: from 1 */
Request setActive(int team, int player);
Request resetStocks();
Request reset();
} // namespace teamBattle

namespace stageStrike {
enum class Command { Undo, Redo, Reset, RpsWin, GameWin };
/** team: the winner (1 or 2), for RpsWin and GameWin */
Request run(int sb, Command command, int team = 1);
} // namespace stageStrike

namespace bracketFocus {
enum class Mode { All, NextRound, PreviousRound, Follow, Tour };
Request set(Mode mode, const std::string &channel = "main", int scoreboard = 1, int tourInterval = 0);
} // namespace bracketFocus

/** The layout pages (HyperDrive with GET /layouts) */
Request layouts();

} // namespace hd
