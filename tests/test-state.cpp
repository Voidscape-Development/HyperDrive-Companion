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

#include "state.hpp"

using namespace hd;

static json sampleState()
{
	return json::parse(R"({
		"score": {
			"1": {
				"best_of": 3,
				"match": "Winners Final",
				"team": {
					"1": {"score": 1, "color": "#ff0000", "player": {"1": {"name": "Alice"}}},
					"2": {"score": "0", "teamName": " The Bs ", "losers": true,
					      "player": {"1": {"name": "Bob"}, "2": {"name": "Bea"}}}
				},
				"stage_strike": {"currPlayer": -1, "strikedStages": [[]], "selectedStage": null, "currGame": 0}
			},
			"2": {"team": {"1": {"player": {}}, "2": {"player": {}}}}
		},
		"display": {"folders": {"scoreboard": true, "top_8": false}, "groups": {"main": true}},
		"stream_queue": {"streams": [{"name": "s", "scoreboard": 1, "sets": [
			{"onStream": true, "team": {"1": {"teamName": "X"}, "2": {"teamName": "Y"}}},
			{"team": {"1": {"player": {"1": {"name": "Carl"}}}, "2": {"player": {"1": {"name": "Dee"}}}}}
		]}]},
		"team_battle": {"battle_mode": "STOCK_POOL", "battle_value": 3, "team": {"1": {
			"sponsor": "Crew A", "active_player": 2,
			"player": {"1": {"name": "P1", "dynamic_spinner": 0, "dead": true},
			           "2": {"name": "P2", "dynamic_spinner": 3}}}}},
		"bracket": {"channels": {"TV 1": {}, "main": {}}, "focus": {"main": {"label": "Round 2"}}}
	})");
}

TEST(state_get)
{
	json state = sampleState();
	CHECK_EQ(text(get(state, "score.1.match")), "Winners Final");
	CHECK(get(state, "score.3") == nullptr);
	CHECK(get(state, "score.1.match.x") == nullptr);
	json list = json::parse(R"({"a": [{"b": 5}]})");
	CHECK_EQ(number(get(list, "a.0.b")), 5.0);
	CHECK(get(list, "a.1") == nullptr);
}

TEST(state_scoreboard)
{
	json state = sampleState();
	CHECK(scoreboardNumbers(state) == std::vector<int>({1, 2}));
	auto t1 = scoreboardTeam(state, 1, 1);
	CHECK(t1.has_value());
	CHECK_EQ(t1->name, "Alice");
	CHECK_EQ(t1->score, 1);
	CHECK_EQ(t1->color, "#ff0000");
	auto t2 = scoreboardTeam(state, 1, 2);
	CHECK_EQ(t2->name, "The Bs");
	CHECK_EQ(t2->score, 0);
	CHECK(t2->losers);
	CHECK_EQ(t2->players.size(), size_t(2));
	CHECK_EQ(bestOf(state, 1), 3);
	CHECK_EQ(winsNeeded(3), 2);
	CHECK_EQ(winsNeeded(5), 3);
	CHECK_EQ(winsNeeded(0), 0);
	CHECK_EQ(matchName(state, 1), "Winners Final");
}

TEST(state_set_key)
{
	json state = sampleState();
	CHECK_EQ(setKey(state, 1), "names:Alice\nThe Bs");
	CHECK_EQ(setKey(state, 2), "");
	// Swapped teams: same set
	json swapped = state;
	std::swap(swapped["score"]["1"]["team"]["1"], swapped["score"]["1"]["team"]["2"]);
	CHECK_EQ(setKey(swapped, 1), setKey(state, 1));
	state["score"]["1"]["set_id"] = 1234;
	CHECK_EQ(setKey(state, 1), "id:1234");
}

TEST(state_next_queued)
{
	json state = sampleState();
	auto next = nextQueuedSet(state, 1);
	CHECK(next.has_value());
	CHECK_EQ(*next, "Carl vs Dee");
	CHECK(!nextQueuedSet(state, 2).has_value());
}

TEST(state_team_battle)
{
	json state = sampleState();
	auto team = teamBattleTeam(state, 1);
	CHECK(team.has_value());
	CHECK_EQ(team->name, "Crew A");
	CHECK_EQ(team->active, 2);
	CHECK_EQ(team->total, 3);
	CHECK(!team->firstTo);
	CHECK(team->players[0].dead);
	CHECK(!teamBattleTeam(state, 2).has_value());
}

TEST(state_display)
{
	json state = sampleState();
	DisplayState display = displayState(state);
	CHECK(display.known);
	CHECK(display.folders["scoreboard"]);
	CHECK(!display.folders["top_8"]);
	CHECK(!display.allShown());
	CHECK(!displayState(json::object()).known);

	auto folder = DisplayTarget::parse("folder:top_8");
	CHECK(folder.has_value());
	CHECK(folder->kind == DisplayTarget::Kind::Folder);
	CHECK_EQ(folder->toString(), "folder:top_8");
	CHECK(DisplayTarget::parse("all").has_value());
	CHECK(!DisplayTarget::parse("bogus:x").has_value());
	CHECK(!DisplayTarget::parse("group:").has_value());
}

TEST(state_strike_and_bracket)
{
	json state = sampleState();
	CHECK(!stageStrikeInfo(state, 1).active);
	state["score"]["1"]["stage_strike"]["strikedStages"] = json::parse(R"([["fd"]])");
	CHECK(stageStrikeInfo(state, 1).active);
	state["score"]["1"]["stage_strike"]["selectedStage"] = "bf";
	StrikeInfo picked = stageStrikeInfo(state, 1);
	CHECK(!picked.active);
	CHECK(picked.stageSelected);
	CHECK_EQ(picked.stage, "bf");

	CHECK(bracketChannels(state) == std::vector<std::string>({"main", "TV 1"}));
	CHECK_EQ(bracketFocusLabel(state, "main"), "Round 2");
	CHECK_EQ(bracketFocusLabel(state, "TV 1"), "");
}

TEST(delta_values_and_removals)
{
	json state = sampleState();
	CHECK(applyDeltas(state, json::parse(R"([
		{"path": ["score", "1", "team", "1", "score"], "action": "values_changed", "value": 2},
		{"path": ["score", "1", "match"], "action": "dictionary_item_removed"},
		{"path": ["new", "deep", "key"], "action": "dictionary_item_added", "value": true}
	])")));
	CHECK_EQ(scoreboardTeam(state, 1, 1)->score, 2);
	CHECK(get(state, "score.1.match") == nullptr);
	CHECK(truthy(get(state, "new.deep.key")));
}

TEST(delta_arrays)
{
	json state = json::parse(R"({"list": [1, 2, 3]})");
	CHECK(applyDelta(state, json::parse(R"({"path": ["list", 1], "action": "iterable_item_removed"})")));
	// The indexes after it stay
	CHECK_EQ(state["list"].size(), size_t(3));
	CHECK(state["list"][1].is_null());
	CHECK(applyDelta(state, json::parse(R"({"path": ["list", 4], "action": "iterable_item_added", "value": 9})")));
	CHECK_EQ(state["list"][4].get<int>(), 9);
	// An integer key in an object is its text
	CHECK(applyDelta(state, json::parse(R"({"path": ["obj", 1], "action": "values_changed", "value": "x"})")));
	CHECK_EQ(text(get(state, "obj.1")), "x");
}

TEST(delta_malformed)
{
	json state = json::object();
	CHECK(!applyDelta(state, json::parse(R"({"action": "values_changed"})")));
	CHECK(!applyDelta(state, json::parse(R"({"path": [], "action": "values_changed"})")));
	CHECK(!applyDeltas(state, json::object()));
}
