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
 * The automation: what happened between two program states, which rules an
 * event runs, and whether their conditions hold.
 */

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "config.hpp"

namespace hd {

struct TriggerEvent {
	TriggerEvent(TriggerKind kind_, int scoreboard_ = 0, std::string param_ = {})
		: kind(kind_),
		  scoreboard(scoreboard_),
		  param(std::move(param_))
	{
	}

	TriggerKind kind;
	/** 0 when not on a scoreboard */
	int scoreboard = 0;
	/** SceneLive: scene; Hotkey: number; Timer: minutes since start; BracketFocusChanged: channel; GameChanged: game codename */
	std::string param;

	bool operator==(const TriggerEvent &other) const
	{
		return kind == other.kind && scoreboard == other.scoreboard && param == other.param;
	}
};

/** The parts of the state detectTriggers reads (to keep a copy between changes) */
nlohmann::json triggerSnapshot(const nlohmann::json &state);

/**
 * The events between two states (or snapshots). Nothing when there was no
 * state before (the plugin just connected), but GameChanged for the game
 * already picked.
 */
std::vector<TriggerEvent> detectTriggers(const nlohmann::json &before, const nlohmann::json &after);

/** The rule runs for this event, from this instance ("" for OBS's events) */
bool ruleMatches(const Rule &rule, const TriggerEvent &event, const std::string &instance);

struct ConditionContext {
	/** The scene on program */
	std::string scene;
	/** The state of the rule's instance, or null */
	const nlohmann::json *state = nullptr;
	/** The event's scoreboard, 0 for none (1 is used) */
	int scoreboard = 0;
	bool recording = false;
	bool streaming = false;
	bool replayBuffer = false;
	/** Ids of the connected instances */
	std::vector<std::string> connected;
};

bool conditionHolds(const Condition &condition, const ConditionContext &context);
bool conditionsHold(const Rule &rule, const ConditionContext &context);

} // namespace hd
