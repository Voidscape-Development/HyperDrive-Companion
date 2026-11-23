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
 * What the plugin saves: the HyperDrive instances it connects to (for all
 * scene collections), and in each scene collection the automation rules
 * (When something happens, Only if conditions hold, Then actions in order),
 * their groups, and what the hotkeys control.
 */

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands.hpp"

namespace hd {

// ---------------------------------------------------------------------
// HyperDrive instances

struct InstanceConfig {
	/** Stable id the sources, rules and hotkeys refer to */
	std::string id;
	std::string name;
	std::string host = "localhost";
	int port = 5500;
	/** "#rrggbb" tag shown next to its name, or "" */
	std::string color;
};

struct GlobalConfig {
	/** At least one; the first is the default */
	std::vector<InstanceConfig> instances;
	/** HyperDrive's folder, to list the layouts when it has no GET /layouts */
	std::string hyperdriveDir;
	bool showAlerts = true;
	/** The instance the dock controls */
	std::string dockInstance;
};

/** Never throws; the single host/port of older versions becomes the "main" instance */
GlobalConfig globalConfigFromJson(const nlohmann::json &json);
nlohmann::json toJson(const GlobalConfig &config);

// ---------------------------------------------------------------------
// Automation

enum class TriggerKind {
	SetFinished,
	SetLoaded,
	ScoreChanged,
	MatchPoint,
	StrikeStarted,
	StrikeEnded,
	QueueChanged,
	BattlePlayerOut,
	BracketFocusChanged,
	Connected,
	Disconnected,
	SceneLive,
	RecordingStarted,
	RecordingStopped,
	StreamingStarted,
	StreamingStopped,
	Hotkey,
	Timer,
	Count
};
enum class ConditionKind {
	SceneIs,
	SceneIsNot,
	BestOf,
	RoundContains,
	TeamHasGames,
	Recording,
	Streaming,
	ReplayBuffer,
	InstanceConnected,
	Count
};
enum class ActionKind {
	SwitchScene,
	ShowLayouts,
	HideLayouts,
	ShowSource,
	HideSource,
	ToggleFilter,
	Command,
	Wait,
	StartRecording,
	StopRecording,
	SaveReplay,
	Count
};

/** "set_finished", "scene_is", "switch_scene"... as saved */
const char *idOf(TriggerKind kind);
const char *idOf(ConditionKind kind);
const char *idOf(ActionKind kind);
bool parseId(const std::string &id, TriggerKind &kind);
bool parseId(const std::string &id, ConditionKind &kind);
bool parseId(const std::string &id, ActionKind &kind);

/** Happens on a HyperDrive instance (the others are OBS's or the plugin's) */
bool triggerOfInstance(TriggerKind kind);
/** Happens on a scoreboard */
bool triggerHasScoreboard(TriggerKind kind);

struct Condition {
	ConditionKind kind = ConditionKind::SceneIs;
	/** Scene, number, text or instance id; nothing for the OBS ones */
	std::string value;
};

/**
 * What target and value mean, by kind: SwitchScene (scene), Show/HideLayouts
 * ("all", "folder:x", "group:x"), Show/HideSource (source, scene or "" for
 * the one live), ToggleFilter (source, filter), Command (a NamedCommand id,
 * scoreboard or "" for the event's), Wait (-, milliseconds).
 */
struct Action {
	ActionKind kind = ActionKind::SwitchScene;
	std::string target;
	std::string value;
};

struct Rule {
	std::string name;
	/** A RuleGroup id, "" for none */
	std::string group;
	bool enabled = true;
	TriggerKind trigger = TriggerKind::SetFinished;
	/** The instance it listens to and commands, "" for any (commands: the event's, or the default) */
	std::string instance;
	/** 0 for any */
	int scoreboard = 0;
	/** SceneLive: scene; Hotkey: 1-10; Timer: minutes; BracketFocusChanged: channel. "" for any */
	std::string param;
	std::vector<Condition> conditions;
	std::vector<Action> actions;
};

struct RuleGroup {
	std::string id;
	std::string name;
	bool enabled = true;
};

struct CollectionConfig {
	bool automationEnabled = true;
	std::vector<RuleGroup> groups;
	std::vector<Rule> rules;
	/** The scoreboard the hotkeys control */
	int hotkeyScoreboard = 1;
	/** The instance the hotkeys control, "" to follow the dock */
	std::string hotkeyInstance;
};

/** Enabled, and so is its group */
bool ruleArmed(const CollectionConfig &config, const Rule &rule);

nlohmann::json toJson(const Rule &rule);
Rule ruleFromJson(const nlohmann::json &json);
nlohmann::json toJson(const CollectionConfig &config);
/** Never throws: what's missing or invalid gets its default. Migrates the older rules and Scenes › Layouts rows */
CollectionConfig collectionConfigFromJson(const nlohmann::json &json);

} // namespace hd
