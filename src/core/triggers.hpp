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
 * What happened between two program states, for the scene automation:
 * a set finished or was loaded, the stage strike started or ended, the
 * score changed.
 */

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "config.hpp"

namespace hd {

struct TriggerEvent {
	TriggerKind kind;
	int scoreboard;

	bool operator==(const TriggerEvent &other) const
	{
		return kind == other.kind && scoreboard == other.scoreboard;
	}
};

/**
 * The events between two states, by scoreboard. Nothing when there was no
 * state before (the plugin just connected).
 */
std::vector<TriggerEvent> detectTriggers(const nlohmann::json &before, const nlohmann::json &after);

/**
 * The rule to run for these events: the first enabled rule (in the list's
 * order) that matches one of them and the scene on program. -1 if none.
 */
int matchRule(const std::vector<AutomationRule> &rules, const std::vector<TriggerEvent> &events,
	      const std::string &currentScene);

/** The scene-display rules for a scene, in order */
std::vector<SceneDisplayRule> sceneDisplayRules(const std::vector<SceneDisplayRule> &rules, const std::string &scene);

} // namespace hd
