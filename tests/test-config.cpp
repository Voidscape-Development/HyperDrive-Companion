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
#include "test.hpp"

#include "config.hpp"

using namespace hd;

TEST(config_round_trip)
{
	CollectionConfig config;
	config.automationEnabled = false;
	config.hotkeyScoreboard = 2;
	config.hotkeyInstance = "b";
	config.groups.push_back({"top8", "Top 8", false});
	Rule rule;
	rule.name = "Victory";
	rule.group = "top8";
	rule.trigger = TriggerKind::MatchPoint;
	rule.instance = "b";
	rule.scoreboard = 1;
	rule.conditions.push_back({ConditionKind::RoundContains, "Final"});
	rule.actions.push_back({ActionKind::Wait, "", "1500"});
	rule.actions.push_back({ActionKind::ShowSource, "Sting", "Game"});
	config.rules.push_back(rule);

	CollectionConfig loaded = collectionConfigFromJson(nlohmann::json::parse(toJson(config).dump()));
	CHECK(!loaded.automationEnabled);
	CHECK_EQ(loaded.hotkeyScoreboard, 2);
	CHECK_EQ(loaded.hotkeyInstance, "b");
	CHECK_EQ(loaded.groups.size(), size_t(1));
	CHECK(!loaded.groups[0].enabled);
	CHECK_EQ(loaded.rules.size(), size_t(1));
	const Rule &r = loaded.rules[0];
	CHECK(r.trigger == TriggerKind::MatchPoint);
	CHECK_EQ(r.name, "Victory");
	CHECK_EQ(r.instance, "b");
	CHECK(r.conditions.size() == 1 && r.conditions[0].kind == ConditionKind::RoundContains);
	CHECK(r.actions.size() == 2 && r.actions[1].kind == ActionKind::ShowSource);
	CHECK_EQ(r.actions[1].value, "Game");
	// Its group is off
	CHECK(!ruleArmed(loaded, r));
	loaded.groups[0].enabled = true;
	CHECK(ruleArmed(loaded, r));
}

TEST(config_migrates_old_rules)
{
	CollectionConfig old = collectionConfigFromJson(nlohmann::json::parse(R"({
		"rules": [{"trigger": "strike_ended", "scoreboard": 1, "scene": "Game", "delay_ms": 1500, "from_scene": "Strike"},
			  {"trigger": "nope"}],
		"scene_display": [{"scene": "Break", "target": "group:main", "action": "hide"}, {"scene": "A", "target": "bogus"}]
	})"));
	CHECK_EQ(old.rules.size(), size_t(2));
	const Rule &switching = old.rules[0];
	CHECK(switching.trigger == TriggerKind::StrikeEnded);
	CHECK(switching.conditions.size() == 1 && switching.conditions[0].value == "Strike");
	CHECK(switching.actions.size() == 2 && switching.actions[0].kind == ActionKind::Wait);
	CHECK_EQ(switching.actions[0].value, "1500");
	CHECK_EQ(switching.actions[1].target, "Game");
	const Rule &display = old.rules[1];
	CHECK(display.trigger == TriggerKind::SceneLive);
	CHECK_EQ(display.param, "Break");
	CHECK(display.actions.size() == 1 && display.actions[0].kind == ActionKind::HideLayouts);
	CHECK_EQ(display.actions[0].target, "group:main");
}

TEST(config_invalid)
{
	CollectionConfig empty = collectionConfigFromJson(nlohmann::json());
	CHECK(empty.automationEnabled);
	CHECK_EQ(empty.hotkeyScoreboard, 1);
	CollectionConfig bad = collectionConfigFromJson(nlohmann::json::parse(R"({
		"hotkey_scoreboard": -3,
		"automation": {"groups": [{"name": "no id"}],
			       "rules": [{"trigger": "nope"}, {"trigger": "timer", "scoreboard": -2,
				"conditions": [{"kind": "bogus"}], "actions": [{"kind": "wait", "value": 3}]}]}
	})"));
	CHECK_EQ(bad.hotkeyScoreboard, 1);
	CHECK(bad.groups.empty());
	CHECK_EQ(bad.rules.size(), size_t(1));
	CHECK_EQ(bad.rules[0].scoreboard, 0);
	CHECK(bad.rules[0].conditions.empty());
	CHECK(bad.rules[0].actions.size() == 1 && bad.rules[0].actions[0].value.empty());
}

TEST(config_global)
{
	// Older versions: one address
	GlobalConfig old =
		globalConfigFromJson(nlohmann::json::parse(R"({"host": "pc", "port": 5501, "show_alerts": false})"));
	CHECK_EQ(old.instances.size(), size_t(1));
	CHECK_EQ(old.instances[0].id, "main");
	CHECK_EQ(old.instances[0].host, "pc");
	CHECK_EQ(old.instances[0].port, 5501);
	CHECK(!old.showAlerts);

	GlobalConfig config = globalConfigFromJson(nlohmann::json::parse(R"({"instances": [
		{"id": "a", "name": "Main", "host": "localhost"}, {"id": "a", "host": "10.0.0.2", "port": 5500}, {}]})"));
	CHECK_EQ(config.instances.size(), size_t(3));
	// Unique ids and a name for each
	CHECK(config.instances[1].id != "a" && !config.instances[1].id.empty());
	CHECK(config.instances[2].id != config.instances[1].id);
	CHECK_EQ(config.instances[1].name, "10.0.0.2:5500");
	GlobalConfig again = globalConfigFromJson(nlohmann::json::parse(toJson(config).dump()));
	CHECK_EQ(again.instances[1].id, config.instances[1].id);
}
