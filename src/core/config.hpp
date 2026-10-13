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
 * What the plugin saves in each OBS scene collection: the scene automation
 * rules, which layouts each scene shows or hides, and the scoreboard the
 * hotkeys control.
 */

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands.hpp"

namespace hd {

enum class TriggerKind { SetFinished, SetLoaded, StrikeStarted, StrikeEnded, ScoreChanged };

/** "set_finished", "set_loaded"... as saved */
const char *triggerId(TriggerKind kind);
bool parseTriggerId(const std::string &id, TriggerKind &kind);
constexpr TriggerKind kAllTriggers[] = {TriggerKind::SetFinished, TriggerKind::SetLoaded, TriggerKind::StrikeStarted,
					TriggerKind::StrikeEnded, TriggerKind::ScoreChanged};

/** When something happens on a scoreboard, switch to a scene */
struct AutomationRule {
	bool enabled = true;
	TriggerKind trigger = TriggerKind::SetFinished;
	/** 0 for any scoreboard */
	int scoreboard = 0;
	/** The scene to switch to */
	std::string scene;
	/** Wait before switching */
	int delayMs = 0;
	/** Only when this scene is on program ("" for any) */
	std::string fromScene;
};

/** When a scene goes on program, show or hide a layout folder, group or everything */
struct SceneDisplayRule {
	std::string scene;
	/** A DisplayTarget: "all", "folder:<name>" or "group:<name>" */
	std::string target = "all";
	/** Show or Hide */
	display::Action action = display::Action::Show;
};

struct CollectionConfig {
	bool automationEnabled = true;
	std::vector<AutomationRule> rules;
	std::vector<SceneDisplayRule> sceneDisplay;
	/** The scoreboard the hotkeys control */
	int hotkeyScoreboard = 1;
};

nlohmann::json toJson(const CollectionConfig &config);
/** Never throws: what's missing or invalid gets its default */
CollectionConfig collectionConfigFromJson(const nlohmann::json &json);

} // namespace hd
