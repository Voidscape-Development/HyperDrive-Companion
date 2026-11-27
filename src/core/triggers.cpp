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
#include "triggers.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

#include "state.hpp"

namespace hd {

namespace {

struct ScoreSnapshot {
	/** Scores by team name, when both teams have different names */
	std::map<std::string, int> byName;
	int scores[2] = {0, 0};
	int best = 0;
};

ScoreSnapshot scores(const nlohmann::json &state, int sb)
{
	ScoreSnapshot snapshot;
	std::string names[2];
	for (int t = 1; t <= 2; t++) {
		auto team = scoreboardTeam(state, sb, t);
		snapshot.scores[t - 1] = team ? team->score : 0;
		names[t - 1] = team ? team->name : "";
	}
	if (!names[0].empty() && !names[1].empty() && names[0] != names[1]) {
		snapshot.byName[names[0]] = snapshot.scores[0];
		snapshot.byName[names[1]] = snapshot.scores[1];
	}
	snapshot.best = std::max(snapshot.scores[0], snapshot.scores[1]);
	return snapshot;
}

bool scoresChanged(const ScoreSnapshot &before, const ScoreSnapshot &after)
{
	// Swapping the teams moves the scores, without changing them
	if (!before.byName.empty() && !after.byName.empty())
		return before.byName != after.byName;
	return before.scores[0] != after.scores[0] || before.scores[1] != after.scores[1];
}

int deadPlayers(const nlohmann::json &state)
{
	int dead = 0;
	for (int t = 1; t <= 2; t++) {
		if (auto team = teamBattleTeam(state, t))
			dead += static_cast<int>(std::count_if(team->players.begin(), team->players.end(),
							       [](const BattlePlayer &p) { return p.dead; }));
	}
	return dead;
}

std::string lower(std::string s)
{
	for (char &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

} // namespace

nlohmann::json triggerSnapshot(const nlohmann::json &state)
{
	nlohmann::json snapshot = nlohmann::json::object();
	for (const char *key : {"score", "stream_queue", "team_battle"}) {
		if (const nlohmann::json *part = get(state, key))
			snapshot[key] = *part;
	}
	if (const nlohmann::json *focus = get(state, "bracket.focus"))
		snapshot["bracket"]["focus"] = *focus;
	if (std::string game = currentGame(state); !game.empty())
		snapshot["game"]["codename"] = game;
	return snapshot;
}

std::vector<TriggerEvent> detectTriggers(const nlohmann::json &before, const nlohmann::json &after)
{
	std::vector<TriggerEvent> events;
	std::string game = currentGame(after);
	if (!game.empty() && game != currentGame(before))
		events.push_back({TriggerKind::GameChanged, 0, game});
	const nlohmann::json *beforeScores = get(before, "score");
	if (!beforeScores || !beforeScores->is_object())
		return events;
	std::vector<int> known = scoreboardNumbers(before);

	for (int sb : scoreboardNumbers(after)) {
		if (std::find(known.begin(), known.end(), sb) == known.end())
			continue;

		std::string keyBefore = setKey(before, sb);
		std::string keyAfter = setKey(after, sb);
		if (keyAfter != keyBefore) {
			if (!keyAfter.empty())
				events.push_back({TriggerKind::SetLoaded, sb});
		} else if (!keyAfter.empty()) {
			ScoreSnapshot scoresBefore = scores(before, sb);
			ScoreSnapshot scoresAfter = scores(after, sb);
			if (scoresChanged(scoresBefore, scoresAfter)) {
				int wins = winsNeeded(bestOf(after, sb));
				if (wins > 0 && scoresBefore.best < wins && scoresAfter.best >= wins)
					events.push_back({TriggerKind::SetFinished, sb});
				if (wins > 1 && scoresBefore.best < wins - 1 && scoresAfter.best == wins - 1)
					events.push_back({TriggerKind::MatchPoint, sb});
				events.push_back({TriggerKind::ScoreChanged, sb});
			}
		}

		StrikeInfo strikeBefore = stageStrikeInfo(before, sb);
		StrikeInfo strikeAfter = stageStrikeInfo(after, sb);
		if (!strikeBefore.active && strikeAfter.active)
			events.push_back({TriggerKind::StrikeStarted, sb});
		if (!strikeBefore.stageSelected && strikeAfter.stageSelected)
			events.push_back({TriggerKind::StrikeEnded, sb});
		if (nextQueuedSet(before, sb) != nextQueuedSet(after, sb))
			events.push_back({TriggerKind::QueueChanged, sb});
	}

	if (deadPlayers(after) > deadPlayers(before))
		events.push_back({TriggerKind::BattlePlayerOut});

	const nlohmann::json *focusBefore = get(before, "bracket.focus");
	const nlohmann::json *focusAfter = get(after, "bracket.focus");
	if (focusAfter && focusAfter->is_object()) {
		for (auto it = focusAfter->begin(); it != focusAfter->end(); ++it) {
			bool same = focusBefore && focusBefore->is_object() && focusBefore->contains(it.key()) &&
				    (*focusBefore)[it.key()] == it.value();
			if (!same)
				events.push_back({TriggerKind::BracketFocusChanged, 0, it.key()});
		}
	}
	return events;
}

bool ruleMatches(const Rule &rule, const TriggerEvent &event, const std::string &instance)
{
	if (rule.trigger != event.kind)
		return false;
	if (triggerOfInstance(rule.trigger) && !rule.instance.empty() && rule.instance != instance)
		return false;
	if (triggerHasScoreboard(rule.trigger) && rule.scoreboard != 0 && rule.scoreboard != event.scoreboard)
		return false;
	if (rule.trigger == TriggerKind::Timer) {
		int every = std::atoi(rule.param.c_str());
		return every > 0 && std::atoi(event.param.c_str()) % every == 0;
	}
	return rule.param.empty() || rule.param == event.param;
}

bool conditionHolds(const Condition &c, const ConditionContext &context)
{
	int sb = context.scoreboard > 0 ? context.scoreboard : 1;
	int n = std::atoi(c.value.c_str());
	switch (c.kind) {
	case ConditionKind::SceneIs:
		return context.scene == c.value;
	case ConditionKind::SceneIsNot:
		return context.scene != c.value;
	case ConditionKind::BestOf:
		return context.state && bestOf(*context.state, sb) == n;
	case ConditionKind::RoundContains:
		return context.state && lower(matchName(*context.state, sb)).find(lower(c.value)) != std::string::npos;
	case ConditionKind::TeamHasGames:
		for (int t = 1; context.state && t <= 2; t++) {
			auto team = scoreboardTeam(*context.state, sb, t);
			if (team && team->score == n)
				return true;
		}
		return false;
	case ConditionKind::Recording:
		return context.recording;
	case ConditionKind::Streaming:
		return context.streaming;
	case ConditionKind::ReplayBuffer:
		return context.replayBuffer;
	case ConditionKind::InstanceConnected:
		return std::find(context.connected.begin(), context.connected.end(), c.value) !=
		       context.connected.end();
	case ConditionKind::GameIs:
		return context.state && currentGame(*context.state) == c.value;
	case ConditionKind::GameIsNot:
		return context.state && currentGame(*context.state) != c.value;
	case ConditionKind::Count:
		break;
	}
	return false;
}

bool conditionsHold(const Rule &rule, const ConditionContext &context)
{
	return std::all_of(rule.conditions.begin(), rule.conditions.end(),
			   [&](const Condition &c) { return conditionHolds(c, context); });
}

} // namespace hd
