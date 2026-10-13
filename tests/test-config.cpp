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
	AutomationRule rule;
	rule.trigger = TriggerKind::StrikeEnded;
	rule.scoreboard = 1;
	rule.scene = "Game";
	rule.delayMs = 1500;
	rule.fromScene = "Strike";
	config.rules.push_back(rule);
	config.sceneDisplay.push_back({"Break", "group:main", display::Action::Hide});

	CollectionConfig loaded = collectionConfigFromJson(nlohmann::json::parse(toJson(config).dump()));
	CHECK(!loaded.automationEnabled);
	CHECK_EQ(loaded.hotkeyScoreboard, 2);
	CHECK_EQ(loaded.rules.size(), size_t(1));
	CHECK(loaded.rules[0].trigger == TriggerKind::StrikeEnded);
	CHECK_EQ(loaded.rules[0].delayMs, 1500);
	CHECK_EQ(loaded.rules[0].fromScene, "Strike");
	CHECK_EQ(loaded.sceneDisplay.size(), size_t(1));
	CHECK(loaded.sceneDisplay[0].action == display::Action::Hide);
	CHECK_EQ(loaded.sceneDisplay[0].target, "group:main");
}

TEST(config_invalid)
{
	CollectionConfig empty = collectionConfigFromJson(nlohmann::json());
	CHECK(empty.automationEnabled);
	CHECK_EQ(empty.hotkeyScoreboard, 1);
	CollectionConfig bad = collectionConfigFromJson(nlohmann::json::parse(R"({
		"hotkey_scoreboard": -3,
		"rules": [{"trigger": "nope"}, {"trigger": "score_changed", "delay_ms": -5, "scene": 3}],
		"scene_display": [{"scene": "A", "target": "bogus"}, {"scene": "B"}]
	})"));
	CHECK_EQ(bad.hotkeyScoreboard, 1);
	CHECK_EQ(bad.rules.size(), size_t(1));
	CHECK_EQ(bad.rules[0].delayMs, 0);
	CHECK_EQ(bad.rules[0].scene, "");
	CHECK_EQ(bad.sceneDisplay.size(), size_t(1));
	CHECK_EQ(bad.sceneDisplay[0].target, "all");
}
