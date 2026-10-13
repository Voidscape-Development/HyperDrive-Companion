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
 * HyperDrive's program state (what the layouts get), and what the plugin
 * reads from it. The web server sends the whole state on connect
 * (`program_state`), then the changes (`program_state_update`): DeepDiff's
 * flat deltas, applied the same way as layout/include/globals.js does.
 */

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hd {

using json = nlohmann::json;

/** Applies one {path, action, value} delta. False if it's malformed. */
bool applyDelta(json &state, const json &delta);
/** Applies a list of deltas, stopping at a malformed one (false). */
bool applyDeltas(json &state, const json &deltas);

/** The value at a dotted path, e.g. "score.1.team.2.score", or nullptr. */
const json *get(const json &state, const std::string &path);

/** A JSON value as text (numbers too), or "" */
std::string text(const json *value);
/** A JSON value as a number (numeric strings too), or fallback */
double number(const json *value, double fallback = 0);
bool truthy(const json *value);

/** The keys of an object that are numbers ("1", "2"...), sorted */
std::vector<int> numericKeys(const json *object);

// Scoreboards

/** The scoreboards HyperDrive has, by number (at least {1} once connected) */
std::vector<int> scoreboardNumbers(const json &state);

struct TeamInfo {
	/** The team's name, or its players' names joined by " / " */
	std::string name;
	std::vector<std::string> players;
	int score = 0;
	/** "#rrggbb", or "" */
	std::string color;
	bool losers = false;
};

/**
 * A scoreboard team. Team 1 is the left side: HyperDrive moves the players
 * when the teams are swapped, like the score up/down links expect.
 */
std::optional<TeamInfo> scoreboardTeam(const json &state, int scoreboard, int team);
int bestOf(const json &state, int scoreboard);
/** Games to win a best of: 2 for a best of 3, 0 if unknown */
int winsNeeded(int bestOf);
/** What the set on the scoreboard is called, e.g. "Winners Final", or "" */
std::string matchName(const json &state, int scoreboard);

/**
 * Identifies the set on a scoreboard: its start.gg/parry.gg id if it has
 * one, else its players' names (in either order, so swapping the teams
 * keeps it). "" for an empty scoreboard.
 */
std::string setKey(const json &state, int scoreboard);

/** The set the stream queue showing on this scoreboard loads next, as "A vs B" */
std::optional<std::string> nextQueuedSet(const json &state, int scoreboard);

// Crew/Team Battle

struct BattlePlayer {
	/** From 1, as in /team-battle-team<t>-player<p>-active */
	int number = 0;
	std::string name;
	/** Stocks left (Stock Pool) or games won (First To) */
	int value = 0;
	bool active = false;
	bool dead = false;
};

struct BattleTeam {
	std::string name;
	std::string color;
	std::vector<BattlePlayer> players;
	/** Number of the active player, 0 if none */
	int active = 0;
	int total = 0;
	bool firstTo = false;
	int battleValue = 0;
};

std::optional<BattleTeam> teamBattleTeam(const json &state, int team);

// Display Controls

struct DisplayState {
	/** HyperDrive sent its display state */
	bool known = false;
	std::map<std::string, bool> folders;
	std::map<std::string, bool> groups;
	/** Every folder and group is shown */
	bool allShown() const;
};

DisplayState displayState(const json &state);

/** "all", "folder:<name>" or "group:<name>" */
struct DisplayTarget {
	enum class Kind { All, Folder, Group };
	Kind kind = Kind::All;
	std::string name;

	static std::optional<DisplayTarget> parse(const std::string &value);
	std::string toString() const;
};

// Stage strike

struct StrikeInfo {
	/** The players are striking or picking: not done, but started */
	bool active = false;
	/** The game's stage is chosen */
	bool stageSelected = false;
	std::string stage;
	int game = 0;
};

StrikeInfo stageStrikeInfo(const json &state, int scoreboard);

// Bracket

/** The bracket channels ("main" first), from bracket.channels */
std::vector<std::string> bracketChannels(const json &state);
/** What the bracket focus of a channel shows, e.g. "Winners Round 2", or "" */
std::string bracketFocusLabel(const json &state, const std::string &channel);

} // namespace hd
