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
#include "config.hpp"

#include <algorithm>
#include <iterator>

namespace hd {

namespace {

template<typename T> T value(const nlohmann::json &object, const char *key, T fallback)
{
	if (!object.is_object())
		return fallback;
	auto it = object.find(key);
	if (it == object.end())
		return fallback;
	if constexpr (std::is_same_v<T, bool>) {
		return it->is_boolean() ? it->template get<bool>() : fallback;
	} else if constexpr (std::is_same_v<T, int>) {
		return it->is_number_integer() ? it->template get<int>() : fallback;
	} else {
		return it->is_string() ? it->template get<std::string>() : fallback;
	}
}

const nlohmann::json &array(const nlohmann::json &object, const char *key)
{
	static const nlohmann::json empty = nlohmann::json::array();
	return object.is_object() && object.contains(key) && object[key].is_array() ? object[key] : empty;
}

const char *const kTriggers[] = {"set_finished",
				 "set_loaded",
				 "score_changed",
				 "match_point",
				 "strike_started",
				 "strike_ended",
				 "queue_changed",
				 "battle_player_out",
				 "bracket_focus_changed",
				 "connected",
				 "disconnected",
				 "scene_live",
				 "recording_started",
				 "recording_stopped",
				 "streaming_started",
				 "streaming_stopped",
				 "hotkey",
				 "timer"};
const char *const kConditions[] = {"scene_is",  "scene_is_not", "best_of",       "round_contains",    "team_has_games",
				   "recording", "streaming",    "replay_buffer", "instance_connected"};
const char *const kActions[] = {"switch_scene",    "show_layouts",   "hide_layouts", "show_source",
				"hide_source",     "toggle_filter",  "command",      "wait",
				"start_recording", "stop_recording", "save_replay"};
static_assert(std::size(kTriggers) == static_cast<size_t>(TriggerKind::Count));
static_assert(std::size(kConditions) == static_cast<size_t>(ConditionKind::Count));
static_assert(std::size(kActions) == static_cast<size_t>(ActionKind::Count));

template<typename E, size_t N> bool parse(const char *const (&ids)[N], const std::string &id, E &kind)
{
	for (size_t i = 0; i < N; i++) {
		if (id == ids[i]) {
			kind = static_cast<E>(i);
			return true;
		}
	}
	return false;
}

} // namespace

const char *idOf(TriggerKind kind)
{
	return kTriggers[static_cast<size_t>(kind)];
}
const char *idOf(ConditionKind kind)
{
	return kConditions[static_cast<size_t>(kind)];
}
const char *idOf(ActionKind kind)
{
	return kActions[static_cast<size_t>(kind)];
}
bool parseId(const std::string &id, TriggerKind &kind)
{
	return parse(kTriggers, id, kind);
}
bool parseId(const std::string &id, ConditionKind &kind)
{
	return parse(kConditions, id, kind);
}
bool parseId(const std::string &id, ActionKind &kind)
{
	return parse(kActions, id, kind);
}

bool triggerOfInstance(TriggerKind kind)
{
	return kind <= TriggerKind::Disconnected;
}

bool triggerHasScoreboard(TriggerKind kind)
{
	return kind <= TriggerKind::QueueChanged;
}

// ---------------------------------------------------------------------

GlobalConfig globalConfigFromJson(const nlohmann::json &json)
{
	GlobalConfig config;
	config.hyperdriveDir = value(json, "hyperdrive_dir", std::string());
	config.showAlerts = value(json, "show_alerts", true);
	config.dockInstance = value(json, "dock_instance", std::string());
	for (const nlohmann::json &item : array(json, "instances")) {
		InstanceConfig instance;
		instance.id = value(item, "id", std::string());
		instance.name = value(item, "name", std::string());
		instance.host = value(item, "host", std::string("localhost"));
		instance.port = value(item, "port", 5500);
		instance.color = value(item, "color", std::string());
		config.instances.push_back(instance);
	}
	if (config.instances.empty()) {
		InstanceConfig main{"main", "HyperDrive", value(json, "host", std::string("localhost")),
				    value(json, "port", 5500), ""};
		config.instances.push_back(main);
	}
	// Every instance gets a unique id and a name
	for (size_t i = 0; i < config.instances.size(); i++) {
		InstanceConfig &instance = config.instances[i];
		auto taken = [&](const std::string &id) {
			return id.empty() || std::any_of(config.instances.begin(), config.instances.begin() + i,
							 [&](const InstanceConfig &other) { return other.id == id; });
		};
		for (int n = static_cast<int>(i) + 1; taken(instance.id); n++)
			instance.id = "hd" + std::to_string(n);
		if (instance.name.empty())
			instance.name = instance.host + ":" + std::to_string(instance.port);
	}
	return config;
}

nlohmann::json toJson(const GlobalConfig &config)
{
	nlohmann::json instances = nlohmann::json::array();
	for (const InstanceConfig &i : config.instances)
		instances.push_back(
			{{"id", i.id}, {"name", i.name}, {"host", i.host}, {"port", i.port}, {"color", i.color}});
	return {{"instances", instances},
		{"hyperdrive_dir", config.hyperdriveDir},
		{"show_alerts", config.showAlerts},
		{"dock_instance", config.dockInstance}};
}

// ---------------------------------------------------------------------

bool ruleArmed(const CollectionConfig &config, const Rule &rule)
{
	if (!rule.enabled)
		return false;
	auto group = std::find_if(config.groups.begin(), config.groups.end(),
				  [&](const RuleGroup &g) { return g.id == rule.group; });
	return group == config.groups.end() || group->enabled;
}

nlohmann::json toJson(const Rule &rule)
{
	nlohmann::json conditions = nlohmann::json::array();
	for (const Condition &c : rule.conditions)
		conditions.push_back({{"kind", idOf(c.kind)}, {"value", c.value}});
	nlohmann::json actions = nlohmann::json::array();
	for (const Action &a : rule.actions)
		actions.push_back({{"kind", idOf(a.kind)}, {"target", a.target}, {"value", a.value}});
	return {{"name", rule.name},         {"group", rule.group},
		{"enabled", rule.enabled},   {"trigger", idOf(rule.trigger)},
		{"instance", rule.instance}, {"scoreboard", rule.scoreboard},
		{"param", rule.param},       {"conditions", conditions},
		{"actions", actions}};
}

Rule ruleFromJson(const nlohmann::json &json)
{
	Rule rule;
	rule.name = value(json, "name", std::string());
	rule.group = value(json, "group", std::string());
	rule.enabled = value(json, "enabled", true);
	parseId(value(json, "trigger", std::string()), rule.trigger);
	rule.instance = value(json, "instance", std::string());
	rule.scoreboard = std::max(0, value(json, "scoreboard", 0));
	rule.param = value(json, "param", std::string());
	for (const nlohmann::json &item : array(json, "conditions")) {
		Condition c;
		if (parseId(value(item, "kind", std::string()), c.kind)) {
			c.value = value(item, "value", std::string());
			rule.conditions.push_back(c);
		}
	}
	for (const nlohmann::json &item : array(json, "actions")) {
		Action a;
		if (parseId(value(item, "kind", std::string()), a.kind)) {
			a.target = value(item, "target", std::string());
			a.value = value(item, "value", std::string());
			rule.actions.push_back(a);
		}
	}
	return rule;
}

nlohmann::json toJson(const CollectionConfig &config)
{
	nlohmann::json groups = nlohmann::json::array();
	for (const RuleGroup &g : config.groups)
		groups.push_back({{"id", g.id}, {"name", g.name}, {"enabled", g.enabled}});
	nlohmann::json rules = nlohmann::json::array();
	for (const Rule &rule : config.rules)
		rules.push_back(toJson(rule));
	return {{"automation_enabled", config.automationEnabled},
		{"automation", {{"groups", groups}, {"rules", rules}}},
		{"hotkey_scoreboard", config.hotkeyScoreboard},
		{"hotkey_instance", config.hotkeyInstance}};
}

CollectionConfig collectionConfigFromJson(const nlohmann::json &json)
{
	CollectionConfig config;
	config.automationEnabled = value(json, "automation_enabled", true);
	config.hotkeyScoreboard = std::max(1, value(json, "hotkey_scoreboard", 1));
	config.hotkeyInstance = value(json, "hotkey_instance", std::string());

	if (json.is_object() && json.contains("automation")) {
		const nlohmann::json &automation = json["automation"];
		for (const nlohmann::json &item : array(automation, "groups")) {
			RuleGroup g{value(item, "id", std::string()), value(item, "name", std::string()),
				    value(item, "enabled", true)};
			if (!g.id.empty())
				config.groups.push_back(g);
		}
		for (const nlohmann::json &item : array(automation, "rules")) {
			TriggerKind kind;
			if (parseId(value(item, "trigger", std::string()), kind))
				config.rules.push_back(ruleFromJson(item));
		}
		return config;
	}

	// Older versions: one scene switch per rule, and the Scenes › Layouts rows
	for (const nlohmann::json &item : array(json, "rules")) {
		Rule rule;
		if (!parseId(value(item, "trigger", std::string()), rule.trigger) ||
		    rule.trigger > TriggerKind::StrikeEnded)
			continue;
		rule.enabled = value(item, "enabled", true);
		rule.scoreboard = std::max(0, value(item, "scoreboard", 0));
		std::string from = value(item, "from_scene", std::string());
		if (!from.empty())
			rule.conditions.push_back({ConditionKind::SceneIs, from});
		int delay = std::max(0, value(item, "delay_ms", 0));
		if (delay > 0)
			rule.actions.push_back({ActionKind::Wait, "", std::to_string(delay)});
		rule.actions.push_back({ActionKind::SwitchScene, value(item, "scene", std::string()), ""});
		config.rules.push_back(rule);
	}
	for (const nlohmann::json &item : array(json, "scene_display")) {
		std::string target = value(item, "target", std::string("all"));
		if (!DisplayTarget::parse(target))
			continue;
		Rule rule;
		rule.trigger = TriggerKind::SceneLive;
		rule.param = value(item, "scene", std::string());
		bool hide = value(item, "action", std::string("show")) == "hide";
		rule.actions.push_back({hide ? ActionKind::HideLayouts : ActionKind::ShowLayouts, target, ""});
		config.rules.push_back(rule);
	}
	return config;
}

} // namespace hd
