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

TEST(triggers_match_point_queue_battle_focus)
{
	// Best of 5: 2 games is match point, once
	auto events = detectTriggers(board("A", 1, "B", 1, 5), board("A", 2, "B", 1, 5));
	CHECK(has(events, TriggerKind::MatchPoint));
	CHECK(!has(detectTriggers(board("A", 2, "B", 1, 5), board("A", 2, "B", 2, 5)), TriggerKind::MatchPoint));
	// Best of 1 has none
	CHECK(!has(detectTriggers(board("A", 0, "B", 0, 1), board("A", 1, "B", 0, 1)), TriggerKind::MatchPoint));

	json before = board("A", 0, "B", 0);
	before["stream_queue"]["streams"] = json::array(
		{{{"scoreboard", 1},
		  {"sets", json::array({{{"team", {{"1", {{"teamName", "C"}}}, {"2", {{"teamName", "D"}}}}}}})}}});
	json after = before;
	after["stream_queue"]["streams"][0]["sets"][0]["team"]["1"]["teamName"] = "E";
	CHECK(has(detectTriggers(before, after), TriggerKind::QueueChanged));
	CHECK(!has(detectTriggers(before, before), TriggerKind::QueueChanged));

	before["team_battle"]["team"]["1"]["player"]["1"] = {{"name", "P"}, {"dead", false}};
	after = before;
	after["team_battle"]["team"]["1"]["player"]["1"]["dead"] = true;
	CHECK(has(detectTriggers(before, after), TriggerKind::BattlePlayerOut));

	before["bracket"]["focus"]["main"] = {{"mode", "all"}};
	after = before;
	after["bracket"]["focus"]["main"]["mode"] = "round";
	events = detectTriggers(triggerSnapshot(before), triggerSnapshot(after));
	CHECK(events.size() == 1 && events[0] == (TriggerEvent{TriggerKind::BracketFocusChanged, 0, "main"}));

	// The game: when it changes, and the one already picked on connect
	before["game"] = {{"codename", "ssbu"}, {"name", "Ultimate"}};
	after = before;
	after["game"]["codename"] = "sf6";
	events = detectTriggers(triggerSnapshot(before), triggerSnapshot(after));
	CHECK(events.size() == 1 && events[0] == (TriggerEvent{TriggerKind::GameChanged, 0, "sf6"}));
	CHECK(!has(detectTriggers(before, before), TriggerKind::GameChanged));
	events = detectTriggers(json(), triggerSnapshot(after));
	CHECK(events.size() == 1 && events[0] == (TriggerEvent{TriggerKind::GameChanged, 0, "sf6"}));
	after["game"] = json::object();
	CHECK(detectTriggers(before, after).empty());
}

TEST(triggers_rule_matches)
{
	Rule rule;
	rule.trigger = TriggerKind::SetFinished;
	CHECK(ruleMatches(rule, {TriggerKind::SetFinished, 2}, "a"));
	CHECK(!ruleMatches(rule, {TriggerKind::ScoreChanged, 2}, "a"));
	rule.scoreboard = 1;
	rule.instance = "a";
	CHECK(!ruleMatches(rule, {TriggerKind::SetFinished, 2}, "a"));
	CHECK(ruleMatches(rule, {TriggerKind::SetFinished, 1}, "a"));
	CHECK(!ruleMatches(rule, {TriggerKind::SetFinished, 1}, "b"));

	Rule scene;
	scene.trigger = TriggerKind::SceneLive;
	scene.instance = "a";
	scene.param = "Break";
	// The instance is for commands only: OBS events match from anywhere
	CHECK(ruleMatches(scene, {TriggerKind::SceneLive, 0, "Break"}, ""));
	CHECK(!ruleMatches(scene, {TriggerKind::SceneLive, 0, "Game"}, ""));

	Rule timer;
	timer.trigger = TriggerKind::Timer;
	timer.param = "5";
	CHECK(ruleMatches(timer, {TriggerKind::Timer, 0, "10"}, ""));
	CHECK(!ruleMatches(timer, {TriggerKind::Timer, 0, "11"}, ""));

	Rule game;
	game.trigger = TriggerKind::GameChanged;
	CHECK(ruleMatches(game, {TriggerKind::GameChanged, 0, "sf6"}, "a"));
	game.param = "ssbu";
	CHECK(!ruleMatches(game, {TriggerKind::GameChanged, 0, "sf6"}, "a"));
	CHECK(ruleMatches(game, {TriggerKind::GameChanged, 0, "ssbu"}, "a"));
}

TEST(triggers_conditions)
{
	json state = board("A", 2, "B", 1, 5);
	state["score"]["1"]["match"] = "Grand Final";
	ConditionContext context;
	context.scene = "Game";
	context.state = &state;
	context.recording = true;
	context.connected = {"a"};
	CHECK(conditionHolds({ConditionKind::SceneIs, "Game"}, context));
	CHECK(!conditionHolds({ConditionKind::SceneIsNot, "Game"}, context));
	CHECK(conditionHolds({ConditionKind::BestOf, "5"}, context));
	CHECK(conditionHolds({ConditionKind::TeamHasGames, "2"}, context));
	CHECK(!conditionHolds({ConditionKind::TeamHasGames, "3"}, context));
	CHECK(conditionHolds({ConditionKind::Recording, ""}, context));
	CHECK(!conditionHolds({ConditionKind::Streaming, ""}, context));
	CHECK(conditionHolds({ConditionKind::InstanceConnected, "a"}, context));
	CHECK(!conditionHolds({ConditionKind::InstanceConnected, "b"}, context));
	state["game"]["codename"] = "ssbu";
	CHECK(conditionHolds({ConditionKind::GameIs, "ssbu"}, context));
	CHECK(!conditionHolds({ConditionKind::GameIsNot, "ssbu"}, context));
	CHECK(conditionHolds({ConditionKind::GameIsNot, "sf6"}, context));

	Rule rule;
	CHECK(conditionsHold(rule, context));
	rule.conditions = {{ConditionKind::SceneIs, "Game"}, {ConditionKind::Streaming, ""}};
	CHECK(!conditionsHold(rule, context));
}
