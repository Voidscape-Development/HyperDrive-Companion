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

} // namespace

std::vector<TriggerEvent> detectTriggers(const nlohmann::json &before, const nlohmann::json &after)
{
	std::vector<TriggerEvent> events;
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
				events.push_back({TriggerKind::ScoreChanged, sb});
			}
		}

		StrikeInfo strikeBefore = stageStrikeInfo(before, sb);
		StrikeInfo strikeAfter = stageStrikeInfo(after, sb);
		if (!strikeBefore.active && strikeAfter.active)
			events.push_back({TriggerKind::StrikeStarted, sb});
		if (!strikeBefore.stageSelected && strikeAfter.stageSelected)
			events.push_back({TriggerKind::StrikeEnded, sb});
	}
	return events;
}

int matchRule(const std::vector<AutomationRule> &rules, const std::vector<TriggerEvent> &events,
	      const std::string &currentScene)
{
	for (size_t i = 0; i < rules.size(); i++) {
		const AutomationRule &rule = rules[i];
		if (!rule.enabled || rule.scene.empty())
			continue;
		if (!rule.fromScene.empty() && rule.fromScene != currentScene)
			continue;
		// Already there
		if (rule.scene == currentScene)
			continue;
		for (const TriggerEvent &event : events) {
			if (event.kind == rule.trigger && (rule.scoreboard == 0 || rule.scoreboard == event.scoreboard))
				return static_cast<int>(i);
		}
	}
	return -1;
}

std::vector<SceneDisplayRule> sceneDisplayRules(const std::vector<SceneDisplayRule> &rules, const std::string &scene)
{
	std::vector<SceneDisplayRule> result;
	for (const SceneDisplayRule &rule : rules) {
		if (rule.scene == scene)
			result.push_back(rule);
	}
	return result;
}

} // namespace hd
