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
 * Tools > HyperDrive Companion: the HyperDrive instances, the automation
 * rules and their groups, and what the hotkeys control. The instances are
 * saved for all scene collections, the rest with the scene collection.
 */

#include <functional>
#include <string>
#include <vector>

#include <QDialog>

#include "core/config.hpp"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QSpinBox;
class QTabWidget;
class QTableWidget;

class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	enum Tab { Instances, Automation, Hotkeys };

	explicit SettingsDialog(QWidget *parent = nullptr);

	/** Opens the one dialog on a tab, or brings it up */
	static void showDialog(Tab tab = Instances);

protected:
	void showEvent(QShowEvent *event) override;

private:
	QWidget *buildInstancesTab();
	QWidget *buildAutomationTab();
	QWidget *buildHotkeysTab();

	void load();
	void save();
	void refreshStatus();

	void addInstanceRow(const hd::InstanceConfig &config);
	std::vector<hd::InstanceConfig> instanceRows() const;
	void moveInstance(int delta);

	/** The rule being edited, or null */
	hd::Rule *rule();
	void refreshGroups();
	/** The rule list, then the editor */
	void refreshRules();
	void refreshRuleItem();
	void buildEditor();
	/** Runs once the signal that asked for it is done (it may rebuild its sender) */
	void later(std::function<void()> fn);
	void moveRule(int delta);
	void exportRules();
	void importRules();

	QTabWidget *tabs_ = nullptr;

	QTableWidget *instances_ = nullptr;
	QLineEdit *folder_ = nullptr;
	QCheckBox *alerts_ = nullptr;

	QCheckBox *automationEnabled_ = nullptr;
	QListWidget *groups_ = nullptr;
	QListWidget *rules_ = nullptr;
	QScrollArea *editor_ = nullptr;
	QListWidget *log_ = nullptr;
	hd::CollectionConfig working_;
	int current_ = -1;
	/** The group shown, or all rules */
	bool allGroups_ = true;
	std::string group_;

	QComboBox *hotkeyInstance_ = nullptr;
	QSpinBox *hotkeyScoreboard_ = nullptr;

	QStringList scenes_;
	QStringList sources_;
	QStringList displayTargets_;
	QStringList channels_;
};
