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
#pragma once

/*
 * The HyperDrive dock: the scoreboard, the stream queue, the crew battle,
 * the stage strike, the bracket focus and the Display Controls, live from
 * HyperDrive's state.
 */

#include <QWidget>

#include <nlohmann/json.hpp>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;
class QToolButton;

namespace hd {
struct Request;
}

class HyperDriveDock : public QWidget {
	Q_OBJECT

public:
	explicit HyperDriveDock(QWidget *parent = nullptr);

private:
	struct TeamRow {
		QToolButton *color = nullptr;
		QLabel *name = nullptr;
		QLabel *score = nullptr;
	};
	struct BattleRow {
		QLabel *name = nullptr;
		QComboBox *players = nullptr;
	};

	QWidget *buildScoreTab();
	QWidget *buildQueueTab();
	QWidget *buildBattleTab();
	QWidget *buildStrikeTab();
	QWidget *buildBracketTab();
	QWidget *buildDisplayTab();

	void refresh();
	void refreshStatus();
	void refreshScoreboard(const nlohmann::json &state);
	void refreshBattle(const nlohmann::json &state);
	void refreshStrike(const nlohmann::json &state);
	void refreshBracket(const nlohmann::json &state);
	void refreshDisplay(const nlohmann::json &state);
	void showError(const QString &message);

	int scoreboard() const;
	void send(const hd::Request &request);
	bool confirm(const QString &question);
	void pickColor(int team);

	QLabel *status_ = nullptr;
	QComboBox *scoreboardPicker_ = nullptr;
	QLabel *error_ = nullptr;
	QTimer *errorTimer_ = nullptr;

	TeamRow teams_[2];
	QLabel *matchInfo_ = nullptr;
	QLabel *nextSet_ = nullptr;
	BattleRow battle_[2];
	QLabel *battleInfo_ = nullptr;
	QLabel *strikeInfo_ = nullptr;
	QPushButton *rps_[2] = {nullptr, nullptr};
	QPushButton *gameWin_[2] = {nullptr, nullptr};
	QComboBox *bracketChannel_ = nullptr;
	QLabel *bracketInfo_ = nullptr;
	QListWidget *displayList_ = nullptr;
};
