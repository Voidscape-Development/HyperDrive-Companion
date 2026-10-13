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
#include "hotkeys.hpp"

#include <functional>
#include <string>
#include <vector>

#include <obs-module.h>

#include "companion.hpp"
#include "core/commands.hpp"

namespace {

struct Hotkey {
	const char *name;
	const char *description;
	/** The command, for the hotkeys' scoreboard */
	std::function<hd::Request(int sb)> request;
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

using namespace hd;

std::vector<Hotkey> &hotkeys()
{
	using SC = stageStrike::Command;
	using BF = bracketFocus::Mode;
	static std::vector<Hotkey> all = {
		{"hyperdrive.team1.scoreup", "HyperDrive.Hotkey.Team1ScoreUp",
		 [](int sb) {
			 return scoreboard::scoreUp(sb, 1);
		 }},
		{"hyperdrive.team1.scoredown", "HyperDrive.Hotkey.Team1ScoreDown",
		 [](int sb) {
			 return scoreboard::scoreDown(sb, 1);
		 }},
		{"hyperdrive.team2.scoreup", "HyperDrive.Hotkey.Team2ScoreUp",
		 [](int sb) {
			 return scoreboard::scoreUp(sb, 2);
		 }},
		{"hyperdrive.team2.scoredown", "HyperDrive.Hotkey.Team2ScoreDown",
		 [](int sb) {
			 return scoreboard::scoreDown(sb, 2);
		 }},
		{"hyperdrive.swap", "HyperDrive.Hotkey.Swap",
		 [](int sb) {
			 return scoreboard::swapTeams(sb);
		 }},
		{"hyperdrive.reset_scores", "HyperDrive.Hotkey.ResetScores",
		 [](int sb) {
			 return scoreboard::reset(sb, scoreboard::ResetKind::Scores);
		 }},
		{"hyperdrive.reset_match", "HyperDrive.Hotkey.ResetMatch",
		 [](int sb) {
			 return scoreboard::reset(sb, scoreboard::ResetKind::Match);
		 }},
		{"hyperdrive.load_next", "HyperDrive.Hotkey.LoadNext",
		 [](int sb) {
			 return scoreboard::loadNextQueued(sb);
		 }},
		{"hyperdrive.pull_stream", "HyperDrive.Hotkey.PullStream",
		 [](int sb) {
			 return scoreboard::pullStreamSet(sb);
		 }},
		{"hyperdrive.strike.undo", "HyperDrive.Hotkey.StrikeUndo",
		 [](int sb) {
			 return stageStrike::run(sb, SC::Undo);
		 }},
		{"hyperdrive.strike.redo", "HyperDrive.Hotkey.StrikeRedo",
		 [](int sb) {
			 return stageStrike::run(sb, SC::Redo);
		 }},
		{"hyperdrive.strike.reset", "HyperDrive.Hotkey.StrikeReset",
		 [](int sb) {
			 return stageStrike::run(sb, SC::Reset);
		 }},
		{"hyperdrive.strike.rps1", "HyperDrive.Hotkey.StrikeRps1",
		 [](int sb) {
			 return stageStrike::run(sb, SC::RpsWin, 1);
		 }},
		{"hyperdrive.strike.rps2", "HyperDrive.Hotkey.StrikeRps2",
		 [](int sb) {
			 return stageStrike::run(sb, SC::RpsWin, 2);
		 }},
		{"hyperdrive.strike.win1", "HyperDrive.Hotkey.StrikeWin1",
		 [](int sb) {
			 return stageStrike::run(sb, SC::GameWin, 1);
		 }},
		{"hyperdrive.strike.win2", "HyperDrive.Hotkey.StrikeWin2",
		 [](int sb) {
			 return stageStrike::run(sb, SC::GameWin, 2);
		 }},
		{"hyperdrive.battle.team1.scoreup", "HyperDrive.Hotkey.Battle1ScoreUp",
		 [](int) {
			 return teamBattle::scoreUp(1);
		 }},
		{"hyperdrive.battle.team1.scoredown", "HyperDrive.Hotkey.Battle1ScoreDown",
		 [](int) {
			 return teamBattle::scoreDown(1);
		 }},
		{"hyperdrive.battle.team2.scoreup", "HyperDrive.Hotkey.Battle2ScoreUp",
		 [](int) {
			 return teamBattle::scoreUp(2);
		 }},
		{"hyperdrive.battle.team2.scoredown", "HyperDrive.Hotkey.Battle2ScoreDown",
		 [](int) {
			 return teamBattle::scoreDown(2);
		 }},
		{"hyperdrive.battle.team1.next", "HyperDrive.Hotkey.Battle1Next",
		 [](int) {
			 return teamBattle::nextPlayer(1);
		 }},
		{"hyperdrive.battle.team2.next", "HyperDrive.Hotkey.Battle2Next",
		 [](int) {
			 return teamBattle::nextPlayer(2);
		 }},
		{"hyperdrive.battle.reset_stocks", "HyperDrive.Hotkey.BattleResetStocks",
		 [](int) {
			 return teamBattle::resetStocks();
		 }},
		{"hyperdrive.display.toggle_all", "HyperDrive.Hotkey.DisplayToggleAll",
		 [](int) {
			 return display::set(DisplayTarget(), display::Action::Toggle);
		 }},
		{"hyperdrive.display.show_all", "HyperDrive.Hotkey.DisplayShowAll",
		 [](int) {
			 return display::set(DisplayTarget(), display::Action::Show);
		 }},
		{"hyperdrive.display.hide_all", "HyperDrive.Hotkey.DisplayHideAll",
		 [](int) {
			 return display::set(DisplayTarget(), display::Action::Hide);
		 }},
		{"hyperdrive.bracket.all", "HyperDrive.Hotkey.BracketAll",
		 [](int) {
			 return bracketFocus::set(BF::All);
		 }},
		{"hyperdrive.bracket.next", "HyperDrive.Hotkey.BracketNext",
		 [](int) {
			 return bracketFocus::set(BF::NextRound);
		 }},
		{"hyperdrive.bracket.previous", "HyperDrive.Hotkey.BracketPrevious",
		 [](int) {
			 return bracketFocus::set(BF::PreviousRound);
		 }},
		{"hyperdrive.bracket.follow", "HyperDrive.Hotkey.BracketFollow",
		 [](int sb) {
			 return bracketFocus::set(BF::Follow, "main", sb);
		 }},
	};
	return all;
}

void pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool down)
{
	if (!down)
		return;
	Companion *companion = Companion::instance();
	if (!companion)
		return;
	const Hotkey *hotkey = static_cast<const Hotkey *>(data);
	Companion::commandFromAnyThread(hotkey->request(companion->hotkeyScoreboard()));
}

} // namespace

void hotkeys_register()
{
	for (Hotkey &hotkey : hotkeys()) {
		if (hotkey.id == OBS_INVALID_HOTKEY_ID)
			hotkey.id = obs_hotkey_register_frontend(hotkey.name, obs_module_text(hotkey.description),
								 pressed, &hotkey);
	}
}

void hotkeys_unregister()
{
	for (Hotkey &hotkey : hotkeys()) {
		if (hotkey.id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(hotkey.id);
		hotkey.id = OBS_INVALID_HOTKEY_ID;
	}
}

obs_data_t *hotkeys_save()
{
	obs_data_t *data = obs_data_create();
	for (const Hotkey &hotkey : hotkeys()) {
		if (hotkey.id == OBS_INVALID_HOTKEY_ID)
			continue;
		obs_data_array_t *keys = obs_hotkey_save(hotkey.id);
		obs_data_set_array(data, hotkey.name, keys);
		obs_data_array_release(keys);
	}
	return data;
}

void hotkeys_load(obs_data_t *data)
{
	for (const Hotkey &hotkey : hotkeys()) {
		if (hotkey.id == OBS_INVALID_HOTKEY_ID)
			continue;
		obs_data_array_t *keys = data ? obs_data_get_array(data, hotkey.name) : nullptr;
		if (!keys)
			keys = obs_data_array_create();
		obs_hotkey_load(hotkey.id, keys);
		obs_data_array_release(keys);
	}
}
