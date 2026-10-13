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

} // namespace

const char *triggerId(TriggerKind kind)
{
	switch (kind) {
	case TriggerKind::SetFinished:
		return "set_finished";
	case TriggerKind::SetLoaded:
		return "set_loaded";
	case TriggerKind::StrikeStarted:
		return "strike_started";
	case TriggerKind::StrikeEnded:
		return "strike_ended";
	case TriggerKind::ScoreChanged:
		return "score_changed";
	}
	return "set_finished";
}

bool parseTriggerId(const std::string &id, TriggerKind &kind)
{
	for (TriggerKind candidate : kAllTriggers) {
		if (id == triggerId(candidate)) {
			kind = candidate;
			return true;
		}
	}
	return false;
}

nlohmann::json toJson(const CollectionConfig &config)
{
	nlohmann::json rules = nlohmann::json::array();
	for (const AutomationRule &rule : config.rules) {
		rules.push_back({{"enabled", rule.enabled},
				 {"trigger", triggerId(rule.trigger)},
				 {"scoreboard", rule.scoreboard},
				 {"scene", rule.scene},
				 {"delay_ms", rule.delayMs},
				 {"from_scene", rule.fromScene}});
	}
	nlohmann::json sceneDisplay = nlohmann::json::array();
	for (const SceneDisplayRule &rule : config.sceneDisplay) {
		sceneDisplay.push_back(
			{{"scene", rule.scene}, {"target", rule.target}, {"action", display::actionName(rule.action)}});
	}
	return {{"automation_enabled", config.automationEnabled},
		{"rules", rules},
		{"scene_display", sceneDisplay},
		{"hotkey_scoreboard", config.hotkeyScoreboard}};
}

CollectionConfig collectionConfigFromJson(const nlohmann::json &json)
{
	CollectionConfig config;
	config.automationEnabled = value(json, "automation_enabled", true);
	config.hotkeyScoreboard = value(json, "hotkey_scoreboard", 1);
	if (config.hotkeyScoreboard < 1)
		config.hotkeyScoreboard = 1;

	if (json.is_object() && json.contains("rules") && json["rules"].is_array()) {
		for (const nlohmann::json &item : json["rules"]) {
			AutomationRule rule;
			if (!parseTriggerId(value(item, "trigger", std::string()), rule.trigger))
				continue;
			rule.enabled = value(item, "enabled", true);
			rule.scoreboard = std::max(0, value(item, "scoreboard", 0));
			rule.scene = value(item, "scene", std::string());
			rule.delayMs = std::max(0, value(item, "delay_ms", 0));
			rule.fromScene = value(item, "from_scene", std::string());
			config.rules.push_back(rule);
		}
	}
	if (json.is_object() && json.contains("scene_display") && json["scene_display"].is_array()) {
		for (const nlohmann::json &item : json["scene_display"]) {
			SceneDisplayRule rule;
			rule.scene = value(item, "scene", std::string());
			rule.target = value(item, "target", std::string("all"));
			if (!DisplayTarget::parse(rule.target))
				continue;
			rule.action = value(item, "action", std::string("show")) == "hide" ? display::Action::Hide
											   : display::Action::Show;
			config.sceneDisplay.push_back(rule);
		}
	}
	return config;
}

} // namespace hd
