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

#include "commands.hpp"

using namespace hd;

TEST(commands_scoreboard)
{
	CHECK_EQ(scoreboard::scoreUp(1, 2).path, "/scoreboard1-team2-scoreup");
	CHECK_EQ(scoreboard::scoreDown(2, 1).path, "/scoreboard2-team1-scoredown");
	CHECK_EQ(scoreboard::swapTeams(1).path, "/scoreboard1-swap-teams");
	CHECK_EQ(scoreboard::teamColor(1, 1, "#00ff00").path, "/scoreboard1-team1-color-00ff00");
	CHECK_EQ(scoreboard::reset(1, scoreboard::ResetKind::All).path, "/scoreboard1-clear-all");
	CHECK_EQ(scoreboard::reset(3, scoreboard::ResetKind::Players).path, "/scoreboard3-reset-players");
	CHECK_EQ(scoreboard::loadNextQueued(2).path, "/stream-queue/load-next?scoreboard=2");
	CHECK_EQ(scoreboard::pullStreamSet(1).path, "/scoreboard1-pull-stream");
	CHECK_EQ(scoreboard::openSetSelector(1).path, "/scoreboard1-open-set");
	CHECK_EQ(scoreboard::scoreUp(1, 1).method, "GET");
}

TEST(commands_display)
{
	CHECK_EQ(display::set(*DisplayTarget::parse("all"), display::Action::Toggle).path, "/display/all/toggle");
	CHECK_EQ(display::set(*DisplayTarget::parse("group:top 8"), display::Action::Hide).path,
		 "/display/group/top%208/hide");
	CHECK_EQ(display::set(*DisplayTarget::parse("folder:scoreboard"), display::Action::Show).path,
		 "/display/folder/scoreboard/show");
}

TEST(commands_team_battle)
{
	CHECK_EQ(teamBattle::scoreUp(1).path, "/team-battle-team1-scoreup");
	CHECK_EQ(teamBattle::setActive(2, 3).path, "/team-battle-team2-player3-active");
	CHECK_EQ(teamBattle::nextPlayer(2).path, "/team-battle-team2-next-player");
	CHECK_EQ(teamBattle::resetStocks().path, "/team-battle-reset-stocks");
}

TEST(commands_stage_strike)
{
	Request undo = stageStrike::run(2, stageStrike::Command::Undo);
	CHECK_EQ(undo.path, "/stage_strike_undo");
	CHECK_EQ(undo.method, "POST");
	CHECK_EQ((*undo.body)["scoreboardNumber"].get<int>(), 2);
	CHECK(!undo.body->contains("winner"));
	Request win = stageStrike::run(1, stageStrike::Command::GameWin, 2);
	CHECK_EQ(win.path, "/stage_strike_match_win");
	CHECK_EQ((*win.body)["winner"].get<int>(), 1);
}

TEST(commands_bracket_focus)
{
	CHECK_EQ(bracketFocus::set(bracketFocus::Mode::All).path, "/bracket-focus/all");
	CHECK_EQ(bracketFocus::set(bracketFocus::Mode::Follow, "main", 2).path, "/bracket-focus/follow?scoreboard=2");
	CHECK_EQ(bracketFocus::set(bracketFocus::Mode::NextRound, "TV 1").path,
		 "/bracket-focus/next-round?channel=TV%201");
	CHECK_EQ(bracketFocus::set(bracketFocus::Mode::Tour, "main", 1, 8).path, "/bracket-focus/tour?interval=8");
}

TEST(commands_answers)
{
	CHECK(commandSucceeded(200, "OK"));
	CHECK(commandSucceeded(200, ""));
	CHECK(!commandSucceeded(200, "ERROR: nothing to undo"));
	CHECK(!commandSucceeded(200, "  error"));
	CHECK(commandSucceeded(200, "ERRORS_FIXED"));
	CHECK(!commandSucceeded(404, "File not found"));
	CHECK(!commandSucceeded(0, ""));
}
