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
#include "settings-dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <obs-frontend-api.h>

#include "companion.hpp"
#include "core/config.hpp"
#include "core/state.hpp"
#include "text.hpp"

namespace {

QPointer<SettingsDialog> dialog;

QString q(const std::string &s)
{
	return QString::fromStdString(s);
}

const char *triggerKey(hd::TriggerKind kind)
{
	switch (kind) {
	case hd::TriggerKind::SetFinished:
		return "HyperDrive.Trigger.SetFinished";
	case hd::TriggerKind::SetLoaded:
		return "HyperDrive.Trigger.SetLoaded";
	case hd::TriggerKind::StrikeStarted:
		return "HyperDrive.Trigger.StrikeStarted";
	case hd::TriggerKind::StrikeEnded:
		return "HyperDrive.Trigger.StrikeEnded";
	case hd::TriggerKind::ScoreChanged:
		return "HyperDrive.Trigger.ScoreChanged";
	}
	return "";
}

/** An editable combo box of the scenes (one may have been renamed or removed) */
QComboBox *sceneCombo(const QStringList &scenes, const QString &current, bool allowAny)
{
	QComboBox *combo = new QComboBox();
	if (allowAny)
		combo->addItem(T("HyperDrive.Settings.AnyScene"), QString());
	for (const QString &scene : scenes)
		combo->addItem(scene, scene);
	int index = combo->findData(current);
	if (index < 0 && !current.isEmpty()) {
		combo->addItem(current + QStringLiteral(" ⚠"), current);
		index = combo->count() - 1;
	}
	combo->setCurrentIndex(index >= 0 ? index : 0);
	return combo;
}

QString comboValue(QTableWidget *table, int row, int column)
{
	QComboBox *combo = qobject_cast<QComboBox *>(table->cellWidget(row, column));
	if (!combo)
		return QString();
	QVariant data = combo->currentData();
	return data.isValid() ? data.toString() : combo->currentText();
}

QTableWidget *table(const QStringList &headers, QWidget *parent)
{
	QTableWidget *t = new QTableWidget(0, static_cast<int>(headers.size()), parent);
	t->setHorizontalHeaderLabels(headers);
	t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	t->verticalHeader()->hide();
	t->setSelectionBehavior(QAbstractItemView::SelectRows);
	t->setSelectionMode(QAbstractItemView::SingleSelection);
	return t;
}

} // namespace

void SettingsDialog::showDialog()
{
	if (!dialog) {
		QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
		dialog = new SettingsDialog(main);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(T("HyperDrive.Settings.Title"));
	resize(720, 480);

	QVBoxLayout *layout = new QVBoxLayout(this);
	QTabWidget *tabs = new QTabWidget(this);
	tabs->addTab(buildConnectionTab(), T("HyperDrive.Settings.Tab.Connection"));
	tabs->addTab(buildSceneDisplayTab(), T("HyperDrive.Settings.Tab.SceneDisplay"));
	tabs->addTab(buildAutomationTab(), T("HyperDrive.Settings.Tab.Automation"));
	tabs->addTab(buildHotkeysTab(), T("HyperDrive.Settings.Tab.Hotkeys"));
	layout->addWidget(tabs, 1);

	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		save();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::save);
	layout->addWidget(buttons);

	if (Companion *companion = Companion::instance())
		connect(companion, &Companion::statusChanged, this, &SettingsDialog::refreshStatus);
}

void SettingsDialog::showEvent(QShowEvent *event)
{
	load();
	QDialog::showEvent(event);
}

QWidget *SettingsDialog::buildConnectionTab()
{
	QWidget *tab = new QWidget(this);
	QFormLayout *form = new QFormLayout(tab);
	host_ = new QLineEdit(tab);
	host_->setPlaceholderText(QStringLiteral("localhost"));
	form->addRow(T("HyperDrive.Settings.Host"), host_);
	port_ = new QSpinBox(tab);
	port_->setRange(1, 65535);
	form->addRow(T("HyperDrive.Settings.Port"), port_);
	QLabel *hostHelp = new QLabel(T("HyperDrive.Settings.Host.Help"), tab);
	hostHelp->setWordWrap(true);
	form->addRow(hostHelp);

	QHBoxLayout *folderRow = new QHBoxLayout();
	folder_ = new QLineEdit(tab);
	QPushButton *browse = new QPushButton(T("HyperDrive.Settings.Browse"), tab);
	connect(browse, &QPushButton::clicked, this, [this]() {
		QString dir = QFileDialog::getExistingDirectory(this, T("HyperDrive.Settings.Folder"), folder_->text());
		if (!dir.isEmpty())
			folder_->setText(dir);
	});
	folderRow->addWidget(folder_, 1);
	folderRow->addWidget(browse);
	form->addRow(T("HyperDrive.Settings.Folder"), folderRow);
	QLabel *folderHelp = new QLabel(T("HyperDrive.Settings.Folder.Help"), tab);
	folderHelp->setWordWrap(true);
	form->addRow(folderHelp);

	alerts_ = new QCheckBox(T("HyperDrive.Settings.ShowAlerts"), tab);
	form->addRow(alerts_);
	status_ = new QLabel(tab);
	form->addRow(T("HyperDrive.Settings.Status"), status_);
	return tab;
}

QWidget *SettingsDialog::buildSceneDisplayTab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	QLabel *help = new QLabel(T("HyperDrive.Settings.SceneDisplay.Help"), tab);
	help->setWordWrap(true);
	layout->addWidget(help);
	sceneDisplay_ = table({T("HyperDrive.Settings.Scene"), T("HyperDrive.Settings.Layouts"),
			       T("HyperDrive.Settings.Action")},
			      tab);
	layout->addWidget(sceneDisplay_, 1);

	QHBoxLayout *buttons = new QHBoxLayout();
	QPushButton *add = new QPushButton(T("HyperDrive.Settings.Add"), tab);
	connect(add, &QPushButton::clicked, this,
		[this]() { addSceneDisplayRow(q(Companion::currentSceneName()), QStringLiteral("all"), true); });
	QPushButton *remove = new QPushButton(T("HyperDrive.Settings.Remove"), tab);
	connect(remove, &QPushButton::clicked, this, [this]() {
		if (sceneDisplay_->currentRow() >= 0)
			sceneDisplay_->removeRow(sceneDisplay_->currentRow());
	});
	buttons->addWidget(add);
	buttons->addWidget(remove);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	return tab;
}

QWidget *SettingsDialog::buildAutomationTab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	automationEnabled_ = new QCheckBox(T("HyperDrive.Settings.AutomationEnabled"), tab);
	layout->addWidget(automationEnabled_);
	QLabel *help = new QLabel(T("HyperDrive.Settings.Automation.Help"), tab);
	help->setWordWrap(true);
	layout->addWidget(help);
	rules_ = table({T("HyperDrive.Settings.On"), T("HyperDrive.Settings.When"), T("HyperDrive.Settings.Scoreboard"),
			T("HyperDrive.Settings.SwitchTo"), T("HyperDrive.Settings.Delay"),
			T("HyperDrive.Settings.OnlyFrom")},
		       tab);
	rules_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	layout->addWidget(rules_, 1);

	QHBoxLayout *buttons = new QHBoxLayout();
	QPushButton *add = new QPushButton(T("HyperDrive.Settings.Add"), tab);
	connect(add, &QPushButton::clicked, this,
		[this]() { addRuleRow(true, 0, 0, scenes_.isEmpty() ? QString() : scenes_.first(), 0, QString()); });
	QPushButton *remove = new QPushButton(T("HyperDrive.Settings.Remove"), tab);
	connect(remove, &QPushButton::clicked, this, [this]() {
		if (rules_->currentRow() >= 0)
			rules_->removeRow(rules_->currentRow());
	});
	QPushButton *up = new QPushButton(T("HyperDrive.Settings.Up"), tab);
	connect(up, &QPushButton::clicked, this, [this]() { moveRule(-1); });
	QPushButton *down = new QPushButton(T("HyperDrive.Settings.Down"), tab);
	connect(down, &QPushButton::clicked, this, [this]() { moveRule(1); });
	buttons->addWidget(add);
	buttons->addWidget(remove);
	buttons->addWidget(up);
	buttons->addWidget(down);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	return tab;
}

QWidget *SettingsDialog::buildHotkeysTab()
{
	QWidget *tab = new QWidget(this);
	QFormLayout *form = new QFormLayout(tab);
	hotkeyScoreboard_ = new QSpinBox(tab);
	hotkeyScoreboard_->setRange(1, 32);
	form->addRow(T("HyperDrive.Settings.HotkeyScoreboard"), hotkeyScoreboard_);
	QLabel *help = new QLabel(T("HyperDrive.Settings.Hotkeys.Help"), tab);
	help->setWordWrap(true);
	form->addRow(help);
	return tab;
}

void SettingsDialog::addSceneDisplayRow(const QString &scene, const QString &target, bool show)
{
	int row = sceneDisplay_->rowCount();
	sceneDisplay_->insertRow(row);
	sceneDisplay_->setCellWidget(row, 0, sceneCombo(scenes_, scene, false));

	QComboBox *targets = new QComboBox();
	targets->setEditable(true);
	targets->setToolTip(T("HyperDrive.Settings.Layouts.Tip"));
	targets->addItems(displayTargets_);
	if (targets->findText(target) < 0)
		targets->addItem(target);
	targets->setCurrentText(target);
	sceneDisplay_->setCellWidget(row, 1, targets);

	QComboBox *action = new QComboBox();
	action->addItem(T("HyperDrive.Settings.Show"), QStringLiteral("show"));
	action->addItem(T("HyperDrive.Settings.Hide"), QStringLiteral("hide"));
	action->setCurrentIndex(show ? 0 : 1);
	sceneDisplay_->setCellWidget(row, 2, action);
}

void SettingsDialog::addRuleRow(bool enabled, int trigger, int scoreboard, const QString &scene, int delayMs,
				const QString &fromScene)
{
	int row = rules_->rowCount();
	rules_->insertRow(row);

	QTableWidgetItem *on = new QTableWidgetItem();
	on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
	on->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
	rules_->setItem(row, 0, on);

	QComboBox *when = new QComboBox();
	for (hd::TriggerKind kind : hd::kAllTriggers)
		when->addItem(T(triggerKey(kind)), QString::fromUtf8(hd::triggerId(kind)));
	when->setCurrentIndex(trigger);
	rules_->setCellWidget(row, 1, when);

	QComboBox *sb = new QComboBox();
	sb->addItem(T("HyperDrive.Settings.AnyScoreboard"), 0);
	for (int n = 1; n <= std::max(4, scoreboard); n++)
		sb->addItem(QString::number(n), n);
	sb->setCurrentIndex(std::max(0, sb->findData(scoreboard)));
	rules_->setCellWidget(row, 2, sb);

	rules_->setCellWidget(row, 3, sceneCombo(scenes_, scene, false));

	QDoubleSpinBox *delay = new QDoubleSpinBox();
	delay->setRange(0, 600);
	delay->setDecimals(1);
	delay->setSingleStep(0.5);
	delay->setSuffix(QStringLiteral(" s"));
	delay->setValue(delayMs / 1000.0);
	rules_->setCellWidget(row, 4, delay);

	rules_->setCellWidget(row, 5, sceneCombo(scenes_, fromScene, true));
}

void SettingsDialog::moveRule(int delta)
{
	int row = rules_->currentRow();
	int target = row + delta;
	if (row < 0 || target < 0 || target >= rules_->rowCount())
		return;
	// Rebuild both rows from their values: cell widgets can't be moved
	auto read = [this](int r) {
		hd::AutomationRule rule;
		rule.enabled = rules_->item(r, 0)->checkState() == Qt::Checked;
		hd::parseTriggerId(comboValue(rules_, r, 1).toStdString(), rule.trigger);
		rule.scoreboard = comboValue(rules_, r, 2).toInt();
		rule.scene = comboValue(rules_, r, 3).toStdString();
		rule.delayMs = static_cast<int>(
			qobject_cast<QDoubleSpinBox *>(rules_->cellWidget(r, 4))->value() * 1000.0 + 0.5);
		rule.fromScene = comboValue(rules_, r, 5).toStdString();
		return rule;
	};
	std::vector<hd::AutomationRule> all;
	for (int r = 0; r < rules_->rowCount(); r++)
		all.push_back(read(r));
	std::swap(all[static_cast<size_t>(row)], all[static_cast<size_t>(target)]);
	rules_->setRowCount(0);
	for (const hd::AutomationRule &rule : all)
		addRuleRow(rule.enabled, static_cast<int>(rule.trigger), rule.scoreboard, q(rule.scene), rule.delayMs,
			   q(rule.fromScene));
	rules_->selectRow(target);
}

void SettingsDialog::load()
{
	Companion *companion = Companion::instance();
	if (!companion)
		return;

	scenes_.clear();
	for (const std::string &scene : Companion::sceneNames())
		scenes_ << q(scene);
	displayTargets_ = QStringList{QStringLiteral("all")};
	hd::DisplayState display;
	companion->client().withState([&display](const nlohmann::json &state) { display = hd::displayState(state); });
	for (const auto &folder : display.folders)
		displayTargets_ << QStringLiteral("folder:") + q(folder.first);
	for (const auto &group : display.groups)
		displayTargets_ << QStringLiteral("group:") + q(group.first);
	// Layout folders HyperDrive hasn't seen open yet
	for (const hd::LayoutFolder &folder : companion->layouts()) {
		QString target = QStringLiteral("folder:") + q(folder.folder);
		if (!displayTargets_.contains(target))
			displayTargets_ << target;
	}

	const GlobalSettings &settings = companion->settings();
	host_->setText(q(settings.host));
	port_->setValue(settings.port);
	folder_->setText(q(settings.hyperdriveDir));
	alerts_->setChecked(settings.showAlerts);

	const hd::CollectionConfig &config = companion->config();
	sceneDisplay_->setRowCount(0);
	for (const hd::SceneDisplayRule &rule : config.sceneDisplay)
		addSceneDisplayRow(q(rule.scene), q(rule.target), rule.action != hd::display::Action::Hide);
	automationEnabled_->setChecked(config.automationEnabled);
	rules_->setRowCount(0);
	for (const hd::AutomationRule &rule : config.rules)
		addRuleRow(rule.enabled, static_cast<int>(rule.trigger), rule.scoreboard, q(rule.scene), rule.delayMs,
			   q(rule.fromScene));
	hotkeyScoreboard_->setValue(config.hotkeyScoreboard);
	refreshStatus();
}

void SettingsDialog::save()
{
	Companion *companion = Companion::instance();
	if (!companion)
		return;

	GlobalSettings settings = companion->settings();
	settings.host = host_->text().trimmed().toStdString();
	settings.port = port_->value();
	settings.hyperdriveDir = folder_->text().trimmed().toStdString();
	settings.showAlerts = alerts_->isChecked();
	companion->setSettings(settings);

	hd::CollectionConfig config;
	for (int row = 0; row < sceneDisplay_->rowCount(); row++) {
		hd::SceneDisplayRule rule;
		rule.scene = comboValue(sceneDisplay_, row, 0).toStdString();
		QComboBox *targets = qobject_cast<QComboBox *>(sceneDisplay_->cellWidget(row, 1));
		rule.target = targets ? targets->currentText().trimmed().toStdString() : "all";
		rule.action = comboValue(sceneDisplay_, row, 2) == QStringLiteral("hide") ? hd::display::Action::Hide
											  : hd::display::Action::Show;
		if (!rule.scene.empty() && hd::DisplayTarget::parse(rule.target))
			config.sceneDisplay.push_back(rule);
	}
	config.automationEnabled = automationEnabled_->isChecked();
	for (int row = 0; row < rules_->rowCount(); row++) {
		hd::AutomationRule rule;
		rule.enabled = rules_->item(row, 0)->checkState() == Qt::Checked;
		hd::parseTriggerId(comboValue(rules_, row, 1).toStdString(), rule.trigger);
		rule.scoreboard = comboValue(rules_, row, 2).toInt();
		rule.scene = comboValue(rules_, row, 3).toStdString();
		rule.delayMs = static_cast<int>(
			qobject_cast<QDoubleSpinBox *>(rules_->cellWidget(row, 4))->value() * 1000.0 + 0.5);
		rule.fromScene = comboValue(rules_, row, 5).toStdString();
		config.rules.push_back(rule);
	}
	config.hotkeyScoreboard = hotkeyScoreboard_->value();
	companion->setConfig(config);
	refreshStatus();
}

void SettingsDialog::refreshStatus()
{
	Companion *companion = Companion::instance();
	if (!companion || !status_)
		return;
	const char *key = "HyperDrive.Status.Disconnected";
	switch (companion->client().status()) {
	case hd::ConnectionStatus::Connected:
		key = "HyperDrive.Status.Connected";
		break;
	case hd::ConnectionStatus::Connecting:
		key = "HyperDrive.Status.Connecting";
		break;
	case hd::ConnectionStatus::Disconnected:
		break;
	}
	status_->setText(T(key).arg(q(companion->settings().host) + ":" + QString::number(companion->settings().port)));
}
