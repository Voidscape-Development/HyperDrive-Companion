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
 * The HyperDrive dock: which instance and scoreboard it controls, then the
 * score, the stream queue, the crew battle, the stage strike, the bracket
 * focus and the Display Controls, live from HyperDrive's state, and what the
 * automation is doing.
 */

#include <string>

#include <QWidget>

#include <nlohmann/json.hpp>

class QButtonGroup;
class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
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
		QFrame *card = nullptr;
		QToolButton *color = nullptr;
		QLabel *name = nullptr;
		QLabel *detail = nullptr;
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
	void refreshInstances();
	void refreshStatus();
	void refreshAutomation();
	void refreshScoreboards(const nlohmann::json &state);
	void refreshScoreboard(const nlohmann::json &state);
	void refreshBattle(const nlohmann::json &state);
	void refreshStrike(const nlohmann::json &state);
	void refreshBracket(const nlohmann::json &state);
	void refreshDisplay(const nlohmann::json &state);
	void showError(const QString &message);

	void send(const hd::Request &request);
	bool confirm(const QString &question);
	void pickColor(int team);

	/** The instance the dock controls */
	std::string instance_;
	int scoreboard_ = 1;

	QComboBox *instancePicker_ = nullptr;
	QHBoxLayout *scoreboards_ = nullptr;
	QButtonGroup *scoreboardButtons_ = nullptr;
	QLabel *status_ = nullptr;
	QStackedWidget *pages_ = nullptr;
	QLabel *toast_ = nullptr;
	QTimer *toastTimer_ = nullptr;
	QLabel *automation_ = nullptr;

	QLabel *round_ = nullptr;
	QLabel *pill_ = nullptr;
	TeamRow teams_[2];
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
