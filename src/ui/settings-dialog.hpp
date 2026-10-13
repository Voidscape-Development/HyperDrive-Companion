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
 * Tools > HyperDrive Companion: where HyperDrive is, which layouts each scene
 * shows or hides, the scene automation and the hotkeys' scoreboard. All but
 * the connection are saved with the scene collection.
 */

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit SettingsDialog(QWidget *parent = nullptr);

	/** Opens the one dialog, or brings it up */
	static void showDialog();

protected:
	void showEvent(QShowEvent *event) override;

private:
	QWidget *buildConnectionTab();
	QWidget *buildSceneDisplayTab();
	QWidget *buildAutomationTab();
	QWidget *buildHotkeysTab();

	void load();
	void save();
	void refreshStatus();

	void addSceneDisplayRow(const QString &scene, const QString &target, bool show);
	void addRuleRow(bool enabled, int trigger, int scoreboard, const QString &scene, int delayMs,
			const QString &fromScene);
	void moveRule(int delta);

	QStringList scenes_;
	QStringList displayTargets_;

	QLineEdit *host_ = nullptr;
	QSpinBox *port_ = nullptr;
	QLineEdit *folder_ = nullptr;
	QCheckBox *alerts_ = nullptr;
	QLabel *status_ = nullptr;

	QTableWidget *sceneDisplay_ = nullptr;
	QCheckBox *automationEnabled_ = nullptr;
	QTableWidget *rules_ = nullptr;
	QSpinBox *hotkeyScoreboard_ = nullptr;
};
