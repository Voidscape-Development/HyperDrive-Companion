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

#include "triggers.hpp"

using namespace hd;
using json = nlohmann::json;

static json board(const char *a, int scoreA, const char *b, int scoreB, int bestOf = 3)
{
	json state;
	state["score"]["1"] = {{"best_of", bestOf},
			       {"team",
				{{"1", {{"score", scoreA}, {"player", {{"1", {{"name", a}}}}}}},
				 {"2", {{"score", scoreB}, {"player", {{"1", {{"name", b}}}}}}}}}};
	return state;
}

static bool has(const std::vector<TriggerEvent> &events, TriggerKind kind)
{
	for (const TriggerEvent &e : events) {
		if (e.kind == kind)
			return true;
	}
	return false;
}

TEST(triggers_first_state)
{
	// Just connected: nothing happened
	CHECK(detectTriggers(json::object(), board("A", 0, "B", 0)).empty());
}

TEST(triggers_score_and_finish)
{
	auto events = detectTriggers(board("A", 0, "B", 0), board("A", 1, "B", 0));
	CHECK(has(events, TriggerKind::ScoreChanged));
	CHECK(!has(events, TriggerKind::SetFinished));

	events = detectTriggers(board("A", 1, "B", 1), board("A", 2, "B", 1));
	CHECK(has(events, TriggerKind::SetFinished));
	CHECK(has(events, TriggerKind::ScoreChanged));
	CHECK_EQ(events[0].scoreboard, 1);

	// Already over: going on doesn't finish it again
	events = detectTriggers(board("A", 2, "B", 1), board("A", 3, "B", 1));
	CHECK(!has(events, TriggerKind::SetFinished));

	// Best of 5
	CHECK(!has(detectTriggers(board("A", 1, "B", 1, 5), board("A", 2, "B", 1, 5)), TriggerKind::SetFinished));
	CHECK(has(detectTriggers(board("A", 2, "B", 1, 5), board("A", 3, "B", 1, 5)), TriggerKind::SetFinished));

	// Unknown best of: never finished
	CHECK(!has(detectTriggers(board("A", 1, "B", 1, 0), board("A", 9, "B", 1, 0)), TriggerKind::SetFinished));
}

TEST(triggers_swap_is_not_a_score)
{
	CHECK(detectTriggers(board("A", 1, "B", 0), board("B", 0, "A", 1)).empty());
}

TEST(triggers_new_set)
{
	auto events = detectTriggers(board("A", 2, "B", 1), board("C", 0, "D", 0));
	CHECK(has(events, TriggerKind::SetLoaded));
	// Not a score change of the same set, nor a finish
	CHECK(!has(events, TriggerKind::ScoreChanged));

	// Cleared scoreboard: not a set loaded
	CHECK(!has(detectTriggers(board("A", 0, "B", 0), board("", 0, "", 0)), TriggerKind::SetLoaded));

	// Set ids win over names
	json before = board("A", 0, "B", 0);
	json after = before;
	before["score"]["1"]["set_id"] = 1;
	after["score"]["1"]["set_id"] = 2;
	CHECK(has(detectTriggers(before, after), TriggerKind::SetLoaded));
}

TEST(triggers_stage_strike)
{
	json before = board("A", 0, "B", 0);
	before["score"]["1"]["stage_strike"] = {{"currPlayer", -1}, {"strikedStages", json::array({json::array()})}};
	json striking = before;
	striking["score"]["1"]["stage_strike"]["currPlayer"] = 0;
	auto events = detectTriggers(before, striking);
	CHECK(has(events, TriggerKind::StrikeStarted));

	json picked = striking;
	picked["score"]["1"]["stage_strike"]["selectedStage"] = "fd";
	events = detectTriggers(striking, picked);
	CHECK(has(events, TriggerKind::StrikeEnded));
	CHECK(!has(events, TriggerKind::StrikeStarted));
}

TEST(triggers_match_rule)
{
	std::vector<AutomationRule> rules(3);
	rules[0].trigger = TriggerKind::ScoreChanged;
	rules[0].scene = "Replay";
	rules[0].enabled = false;
	rules[1].trigger = TriggerKind::SetFinished;
	rules[1].scoreboard = 2;
	rules[1].scene = "Break";
	rules[2].trigger = TriggerKind::SetFinished;
	rules[2].scene = "Results";
	rules[2].fromScene = "Game";

	std::vector<TriggerEvent> finished{{TriggerKind::SetFinished, 1}, {TriggerKind::ScoreChanged, 1}};
	CHECK_EQ(matchRule(rules, finished, "Game"), 2);
	// Only from the Game scene
	CHECK_EQ(matchRule(rules, finished, "Other"), -1);
	// Scoreboard 2 only
	CHECK_EQ(matchRule(rules, {{TriggerKind::SetFinished, 2}}, "Other"), 1);
	// Already on it
	CHECK_EQ(matchRule(rules, {{TriggerKind::SetFinished, 2}}, "Break"), -1);
	rules[0].enabled = true;
	CHECK_EQ(matchRule(rules, finished, "Game"), 0);
}

TEST(triggers_scene_display_rules)
{
	std::vector<SceneDisplayRule> rules{{"Game", "folder:scoreboard", display::Action::Show},
					    {"Break", "folder:scoreboard", display::Action::Hide},
					    {"Game", "group:main", display::Action::Show}};
	auto game = sceneDisplayRules(rules, "Game");
	CHECK_EQ(game.size(), size_t(2));
	CHECK_EQ(game[1].target, "group:main");
	CHECK(sceneDisplayRules(rules, "None").empty());
}
