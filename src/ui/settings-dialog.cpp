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
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <obs-frontend-api.h>

#include "companion.hpp"
#include "core/state.hpp"
#include "style.hpp"
#include "text.hpp"

namespace {

QPointer<SettingsDialog> dialog;

const char *kTagColors[] = {"#f2b53a", "#3e7bfa", "#3dd68c", "#e5484d", "#b48cff", "#ff8a3d"};

QString q(const std::string &s)
{
	return QString::fromStdString(s);
}

/** Choices: what's shown, and the value saved */
using Items = QList<QPair<QString, QString>>;

Items named(const QStringList &list, const QString &none = QString())
{
	Items items;
	if (!none.isNull())
		items << qMakePair(none, QString());
	for (const QString &s : list)
		items << qMakePair(s, s);
	return items;
}

Items instanceItems(const QString &none)
{
	Items items{qMakePair(none, QString())};
	if (Companion *companion = Companion::instance())
		for (const auto &drive : companion->hyperdrives())
			items << qMakePair(q(drive->config.name), q(drive->config.id));
	return items;
}

/**
 * A combo box that sets a value. An editable one also takes what's typed;
 * a value it doesn't list is kept (a renamed or removed scene...).
 */
QComboBox *choice(const Items &items, const std::string &current, bool editable,
		  const std::function<void(const std::string &)> &set)
{
	QComboBox *combo = new QComboBox();
	combo->setEditable(editable);
	combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	combo->setMinimumContentsLength(8);
	for (const auto &item : items)
		combo->addItem(item.first, item.second);
	int index = combo->findData(q(current));
	if (index < 0 && !current.empty()) {
		combo->addItem(editable ? q(current) : q(current) + QStringLiteral(" ⚠"), q(current));
		index = combo->count() - 1;
	}
	combo->setCurrentIndex(std::max(0, index));
	auto value = [combo]() {
		int i = combo->findText(combo->currentText());
		return (i >= 0 ? combo->itemData(i).toString() : combo->currentText()).toStdString();
	};
	QObject::connect(combo, &QComboBox::currentTextChanged, combo, [value, set]() { set(value()); });
	// What it shows is what's saved
	if (value() != current)
		set(value());
	return combo;
}

QToolButton *tool(const QString &text, const QString &tip, QWidget *parent, const std::function<void()> &onClick)
{
	QToolButton *b = new QToolButton(parent);
	b->setText(text);
	b->setToolTip(tip);
	QObject::connect(b, &QToolButton::clicked, parent, onClick);
	return b;
}

QPushButton *push(const char *key, QWidget *parent, const std::function<void()> &onClick)
{
	QPushButton *b = new QPushButton(T(key), parent);
	QObject::connect(b, &QPushButton::clicked, parent, onClick);
	return b;
}

QLabel *help(const char *key, QWidget *parent)
{
	QLabel *label = new QLabel(T(key), parent);
	label->setWordWrap(true);
	return label;
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

QString describe(const hd::Action &a)
{
	QString what = kindText(a.kind);
	if (a.kind == hd::ActionKind::Command) {
		for (const hd::NamedCommand &command : hd::namedCommands())
			if (a.target == command.id)
				return what + QStringLiteral(": ") +
				       T(command.textKey).section(QStringLiteral(": "), -1);
	}
	return a.target.empty() ? what : what + QStringLiteral(": ") + q(a.target);
}

QString ruleText(const hd::Rule &r)
{
	QString then = QStringLiteral("…");
	for (const hd::Action &a : r.actions) {
		if (a.kind != hd::ActionKind::Wait) {
			then = describe(a);
			break;
		}
	}
	int more = static_cast<int>(r.actions.size()) - 1;
	return QStringLiteral("%1\n%2 → %3%4")
		.arg(r.name.empty() ? kindText(r.trigger) : q(r.name), kindText(r.trigger), then,
		     more > 0 ? QStringLiteral(" +%1").arg(more) : QString());
}

} // namespace

void SettingsDialog::showDialog(Tab tab)
{
	if (!dialog) {
		QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
		dialog = new SettingsDialog(main);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	dialog->tabs_->setCurrentIndex(tab);
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(T("HyperDrive.Settings.Title"));
	resize(1000, 640);

	QVBoxLayout *layout = new QVBoxLayout(this);
	tabs_ = new QTabWidget(this);
	tabs_->addTab(buildInstancesTab(), T("HyperDrive.Settings.Tab.Instances"));
	tabs_->addTab(buildAutomationTab(), T("HyperDrive.Settings.Tab.Automation"));
	tabs_->addTab(buildHotkeysTab(), T("HyperDrive.Settings.Tab.Hotkeys"));
	layout->addWidget(tabs_, 1);

	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		save();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::save);
	layout->addWidget(buttons);

	if (Companion *companion = Companion::instance()) {
		connect(companion, &Companion::statusChanged, this, &SettingsDialog::refreshStatus);
		connect(companion, &Companion::automationRan, this, [this](const QString &line) {
			log_->addItem(line);
			log_->scrollToBottom();
		});
	}
}

void SettingsDialog::showEvent(QShowEvent *event)
{
	load();
	QDialog::showEvent(event);
}

void SettingsDialog::later(std::function<void()> fn)
{
	QTimer::singleShot(0, this, std::move(fn));
}

// ---------------------------------------------------------------------
// Instances

QWidget *SettingsDialog::buildInstancesTab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	layout->addWidget(help("HyperDrive.Settings.Instances.Help", tab));
	instances_ = table({QString(), T("HyperDrive.Settings.Name"), T("HyperDrive.Settings.Address"),
			    T("HyperDrive.Settings.Status")},
			   tab);
	instances_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	layout->addWidget(instances_, 1);

	QHBoxLayout *buttons = new QHBoxLayout();
	buttons->addWidget(push("HyperDrive.Settings.Add", tab, [this]() {
		int n = instances_->rowCount();
		addInstanceRow({"", T("HyperDrive.Settings.NewInstance").arg(n + 1).toStdString(), "localhost", 5500,
				kTagColors[n % std::size(kTagColors)]});
		instances_->editItem(instances_->item(n, 1));
	}));
	buttons->addWidget(push("HyperDrive.Settings.Remove", tab, [this]() {
		if (instances_->rowCount() > 1 && instances_->currentRow() >= 0)
			instances_->removeRow(instances_->currentRow());
	}));
	buttons->addWidget(push("HyperDrive.Settings.Up", tab, [this]() { moveInstance(-1); }));
	buttons->addWidget(push("HyperDrive.Settings.Down", tab, [this]() { moveInstance(1); }));
	buttons->addStretch(1);
	layout->addLayout(buttons);
	layout->addWidget(help("HyperDrive.Settings.Host.Help", tab));

	QFormLayout *form = new QFormLayout();
	QHBoxLayout *folderRow = new QHBoxLayout();
	folder_ = new QLineEdit(tab);
	folderRow->addWidget(folder_, 1);
	folderRow->addWidget(push("HyperDrive.Settings.Browse", tab, [this]() {
		QString dir = QFileDialog::getExistingDirectory(this, T("HyperDrive.Settings.Folder"), folder_->text());
		if (!dir.isEmpty())
			folder_->setText(dir);
	}));
	form->addRow(T("HyperDrive.Settings.Folder"), folderRow);
	form->addRow(help("HyperDrive.Settings.Folder.Help", tab));
	alerts_ = new QCheckBox(T("HyperDrive.Settings.ShowAlerts"), tab);
	form->addRow(alerts_);
	layout->addLayout(form);
	return tab;
}

void SettingsDialog::addInstanceRow(const hd::InstanceConfig &config)
{
	int row = instances_->rowCount();
	instances_->insertRow(row);
	QToolButton *color = new QToolButton();
	color->setAutoRaise(true);
	color->setToolTip(T("HyperDrive.Settings.Color"));
	auto setColor = [color](const QString &c) {
		color->setProperty("hdColor", c);
		color->setIcon(style::swatch(c));
	};
	setColor(q(config.color));
	connect(color, &QToolButton::clicked, this, [this, color, setColor]() {
		QColor c = QColorDialog::getColor(QColor(color->property("hdColor").toString()), this,
						  T("HyperDrive.Settings.Color"));
		if (c.isValid())
			setColor(c.name());
	});
	instances_->setCellWidget(row, 0, color);
	QTableWidgetItem *name = new QTableWidgetItem(q(config.name));
	name->setData(Qt::UserRole, q(config.id));
	instances_->setItem(row, 1, name);
	instances_->setItem(row, 2, new QTableWidgetItem(q(config.host) + ":" + QString::number(config.port)));
	QTableWidgetItem *status = new QTableWidgetItem();
	status->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
	instances_->setItem(row, 3, status);
}

std::vector<hd::InstanceConfig> SettingsDialog::instanceRows() const
{
	std::vector<hd::InstanceConfig> rows;
	for (int row = 0; row < instances_->rowCount(); row++) {
		hd::InstanceConfig config;
		config.id = instances_->item(row, 1)->data(Qt::UserRole).toString().toStdString();
		config.name = instances_->item(row, 1)->text().trimmed().toStdString();
		hd::Address address = hd::parseAddress(instances_->item(row, 2)->text().trimmed().toStdString(), 5500);
		config.host = address.host;
		config.port = address.port;
		config.color = instances_->cellWidget(row, 0)->property("hdColor").toString().toStdString();
		rows.push_back(config);
	}
	return rows;
}

void SettingsDialog::moveInstance(int delta)
{
	int row = instances_->currentRow();
	int target = row + delta;
	if (row < 0 || target < 0 || target >= instances_->rowCount())
		return;
	// Cell widgets can't be moved: rebuilt from their values
	std::vector<hd::InstanceConfig> rows = instanceRows();
	std::swap(rows[static_cast<size_t>(row)], rows[static_cast<size_t>(target)]);
	instances_->setRowCount(0);
	for (const hd::InstanceConfig &config : rows)
		addInstanceRow(config);
	instances_->selectRow(target);
	refreshStatus();
}

void SettingsDialog::refreshStatus()
{
	Companion *companion = Companion::instance();
	if (!companion || !instances_)
		return;
	for (int row = 0; row < instances_->rowCount(); row++) {
		std::string id = instances_->item(row, 1)->data(Qt::UserRole).toString().toStdString();
		QString text;
		for (const auto &drive : companion->hyperdrives()) {
			if (drive->config.id != id)
				continue;
			switch (drive->client->status()) {
			case hd::ConnectionStatus::Connected:
				text = QStringLiteral("● ") + T("HyperDrive.Settings.Online");
				break;
			case hd::ConnectionStatus::Connecting:
				text = QStringLiteral("● ") + T("HyperDrive.Settings.Connecting");
				break;
			case hd::ConnectionStatus::Disconnected:
				text = QStringLiteral("● ") + T("HyperDrive.Settings.Offline");
				break;
			}
		}
		instances_->item(row, 3)->setText(
			row == 0 ? text + QStringLiteral(" · ") + T("HyperDrive.Settings.Default") : text);
	}
}

// ---------------------------------------------------------------------
// Automation

QWidget *SettingsDialog::buildAutomationTab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	automationEnabled_ = new QCheckBox(T("HyperDrive.Settings.AutomationEnabled"), tab);
	layout->addWidget(automationEnabled_);
	layout->addWidget(help("HyperDrive.Settings.Automation.Help", tab));

	QSplitter *split = new QSplitter(tab);
	// Groups
	QWidget *groupsColumn = new QWidget(split);
	QVBoxLayout *groupsLayout = new QVBoxLayout(groupsColumn);
	groupsLayout->setContentsMargins(0, 0, 0, 0);
	groupsLayout->addWidget(new QLabel(T("HyperDrive.Automation.Groups"), groupsColumn));
	groups_ = new QListWidget(groupsColumn);
	groups_->setToolTip(T("HyperDrive.Automation.Groups.Tip"));
	groupsLayout->addWidget(groups_, 1);
	connect(groups_, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row < 0)
			return;
		allGroups_ = row == 0;
		group_ = groups_->item(row)->data(Qt::UserRole).toString().toStdString();
		refreshRules();
	});
	connect(groups_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
		std::string id = item->data(Qt::UserRole).toString().toStdString();
		for (hd::RuleGroup &g : working_.groups) {
			if (g.id == id) {
				g.name = item->text().trimmed().toStdString();
				g.enabled = item->checkState() == Qt::Checked;
			}
		}
		later([this]() { refreshRules(); });
	});
	QHBoxLayout *groupButtons = new QHBoxLayout();
	groupButtons->addWidget(tool(QStringLiteral("+"), T("HyperDrive.Automation.AddGroup"), groupsColumn, [this]() {
		std::string id = "g" + std::to_string(QDateTime::currentMSecsSinceEpoch());
		working_.groups.push_back({id, T("HyperDrive.Automation.NewGroup").toStdString(), true});
		refreshGroups();
		groups_->setCurrentRow(groups_->count() - 1);
		groups_->editItem(groups_->currentItem());
	}));
	groupButtons->addWidget(
		tool(QStringLiteral("−"), T("HyperDrive.Automation.RemoveGroup"), groupsColumn, [this]() {
			if (allGroups_)
				return;
			// Its rules stay, without a group
			for (hd::Rule &r : working_.rules)
				if (r.group == group_)
					r.group.clear();
			auto &groups = working_.groups;
			groups.erase(std::remove_if(groups.begin(), groups.end(),
						    [this](const hd::RuleGroup &g) { return g.id == group_; }),
				     groups.end());
			allGroups_ = true;
			refreshGroups();
		}));
	groupButtons->addStretch(1);
	groupButtons->addWidget(tool(T("HyperDrive.Automation.Import"), T("HyperDrive.Automation.Import.Tip"),
				     groupsColumn, [this]() { importRules(); }));
	groupButtons->addWidget(tool(T("HyperDrive.Automation.Export"), T("HyperDrive.Automation.Export.Tip"),
				     groupsColumn, [this]() { exportRules(); }));
	groupsLayout->addLayout(groupButtons);

	// Rules
	QWidget *rulesColumn = new QWidget(split);
	QVBoxLayout *rulesLayout = new QVBoxLayout(rulesColumn);
	rulesLayout->setContentsMargins(0, 0, 0, 0);
	rulesLayout->addWidget(new QLabel(T("HyperDrive.Automation.Rules"), rulesColumn));
	rules_ = new QListWidget(rulesColumn);
	rules_->setSpacing(2);
	rulesLayout->addWidget(rules_, 1);
	connect(rules_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
		if (!item || item->data(Qt::UserRole).toInt() == current_)
			return;
		current_ = item->data(Qt::UserRole).toInt();
		later([this]() { buildEditor(); });
	});
	connect(rules_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
		size_t i = static_cast<size_t>(item->data(Qt::UserRole).toInt());
		if (i < working_.rules.size())
			working_.rules[i].enabled = item->checkState() == Qt::Checked;
	});
	QHBoxLayout *ruleButtons = new QHBoxLayout();
	ruleButtons->addWidget(push("HyperDrive.Automation.NewRule", rulesColumn, [this]() {
		hd::Rule r;
		r.group = allGroups_ ? "" : group_;
		r.actions.push_back({hd::ActionKind::SwitchScene, "", ""});
		working_.rules.push_back(r);
		current_ = static_cast<int>(working_.rules.size()) - 1;
		refreshRules();
	}));
	ruleButtons->addStretch(1);
	ruleButtons->addWidget(
		tool(QStringLiteral("▲"), T("HyperDrive.Settings.Up"), rulesColumn, [this]() { moveRule(-1); }));
	ruleButtons->addWidget(
		tool(QStringLiteral("▼"), T("HyperDrive.Settings.Down"), rulesColumn, [this]() { moveRule(1); }));
	rulesLayout->addLayout(ruleButtons);

	editor_ = new QScrollArea(split);
	editor_->setWidgetResizable(true);
	split->addWidget(groupsColumn);
	split->addWidget(rulesColumn);
	split->addWidget(editor_);
	split->setStretchFactor(0, 2);
	split->setStretchFactor(1, 3);
	split->setStretchFactor(2, 6);
	layout->addWidget(split, 1);

	layout->addWidget(new QLabel(T("HyperDrive.Automation.Activity"), tab));
	log_ = new QListWidget(tab);
	log_->setMaximumHeight(110);
	log_->setToolTip(T("HyperDrive.Automation.Activity.Tip"));
	layout->addWidget(log_);
	return tab;
}

hd::Rule *SettingsDialog::rule()
{
	return current_ >= 0 && static_cast<size_t>(current_) < working_.rules.size()
		       ? &working_.rules[static_cast<size_t>(current_)]
		       : nullptr;
}

void SettingsDialog::refreshGroups()
{
	{
		QSignalBlocker block(groups_);
		groups_->clear();
		QListWidgetItem *all = new QListWidgetItem(T("HyperDrive.Automation.AllRules"), groups_);
		all->setData(Qt::UserRole, QString());
		int selected = 0;
		for (const hd::RuleGroup &g : working_.groups) {
			QListWidgetItem *item = new QListWidgetItem(q(g.name), groups_);
			item->setData(Qt::UserRole, q(g.id));
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable |
				       Qt::ItemIsEditable);
			item->setCheckState(g.enabled ? Qt::Checked : Qt::Unchecked);
			if (!allGroups_ && g.id == group_)
				selected = groups_->count() - 1;
		}
		allGroups_ = selected == 0;
		groups_->setCurrentRow(selected);
	}
	refreshRules();
}

void SettingsDialog::refreshRules()
{
	{
		QSignalBlocker block(rules_);
		rules_->clear();
		int first = -1;
		bool currentShown = false;
		for (size_t i = 0; i < working_.rules.size(); i++) {
			const hd::Rule &r = working_.rules[i];
			if (!allGroups_ && r.group != group_)
				continue;
			QListWidgetItem *item = new QListWidgetItem(ruleText(r), rules_);
			item->setData(Qt::UserRole, static_cast<int>(i));
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
			item->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
			if (!hd::ruleArmed(working_, r))
				item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
			if (first < 0)
				first = static_cast<int>(i);
			if (static_cast<int>(i) == current_) {
				rules_->setCurrentItem(item);
				currentShown = true;
			}
		}
		if (!currentShown) {
			current_ = first;
			if (rules_->count() > 0)
				rules_->setCurrentRow(0);
		}
	}
	buildEditor();
}

void SettingsDialog::refreshRuleItem()
{
	for (int row = 0; row < rules_->count(); row++) {
		if (rules_->item(row)->data(Qt::UserRole).toInt() == current_ && rule())
			rules_->item(row)->setText(ruleText(*rule()));
	}
}

void SettingsDialog::moveRule(int delta)
{
	int target = current_ + delta;
	if (!rule() || target < 0 || static_cast<size_t>(target) >= working_.rules.size())
		return;
	std::swap(working_.rules[static_cast<size_t>(current_)], working_.rules[static_cast<size_t>(target)]);
	current_ = target;
	refreshRules();
}

void SettingsDialog::buildEditor()
{
	using K = hd::ActionKind;
	using C = hd::ConditionKind;
	QWidget *page = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(page);
	hd::Rule *r = rule();
	if (!r) {
		layout->addWidget(help("HyperDrive.Automation.NoRule", page));
		layout->addStretch(1);
		editor_->setWidget(page);
		return;
	}
	auto edited = [this]() {
		refreshRuleItem();
	};

	QFormLayout *top = new QFormLayout();
	QLineEdit *name = new QLineEdit(q(r->name), page);
	name->setPlaceholderText(kindText(r->trigger));
	connect(name, &QLineEdit::textEdited, this, [this, edited](const QString &text) {
		rule()->name = text.toStdString();
		edited();
	});
	top->addRow(T("HyperDrive.Automation.Name"), name);
	Items groups{qMakePair(T("HyperDrive.Automation.NoGroup"), QString())};
	for (const hd::RuleGroup &g : working_.groups)
		groups << qMakePair(q(g.name), q(g.id));
	top->addRow(T("HyperDrive.Automation.Group"), choice(groups, r->group, false, [this](const std::string &v) {
			    if (rule()->group != v) {
				    rule()->group = v;
				    later([this]() { refreshRules(); });
			    }
		    }));
	layout->addLayout(top);

	// When
	QGroupBox *when = new QGroupBox(T("HyperDrive.Automation.When"), page);
	QFormLayout *whenForm = new QFormLayout(when);
	Items triggers;
	for (int k = 0; k < static_cast<int>(hd::TriggerKind::Count); k++)
		triggers << qMakePair(kindText(hd::TriggerKind(k)), QString(hd::idOf(hd::TriggerKind(k))));
	whenForm->addRow(choice(triggers, hd::idOf(r->trigger), false, [this](const std::string &v) {
		hd::TriggerKind kind;
		if (hd::parseId(v, kind) && kind != rule()->trigger) {
			rule()->trigger = kind;
			rule()->param.clear();
			later([this]() { refreshRules(); });
		}
	}));
	whenForm->addRow(T("HyperDrive.Instance"),
			 choice(instanceItems(T("HyperDrive.Automation.AnyInstance")), r->instance, false,
				[this](const std::string &v) { rule()->instance = v; }));
	if (hd::triggerHasScoreboard(r->trigger)) {
		Items boards{qMakePair(T("HyperDrive.Settings.AnyScoreboard"), QStringLiteral("0"))};
		for (int n = 1; n <= std::max(4, r->scoreboard); n++)
			boards << qMakePair(QString::number(n), QString::number(n));
		whenForm->addRow(T("HyperDrive.Settings.Scoreboard"),
				 choice(boards, std::to_string(r->scoreboard), false,
					[this](const std::string &v) { rule()->scoreboard = std::atoi(v.c_str()); }));
	}
	auto setParam = [this, edited](const std::string &v) {
		rule()->param = v;
		edited();
	};
	switch (r->trigger) {
	case hd::TriggerKind::SceneLive:
		whenForm->addRow(T("HyperDrive.Settings.Scene"),
				 choice(named(scenes_, T("HyperDrive.Settings.AnyScene")), r->param, true, setParam));
		break;
	case hd::TriggerKind::Hotkey: {
		Items keys{qMakePair(T("HyperDrive.Automation.AnyHotkey"), QString())};
		for (int n = 1; n <= 10; n++)
			keys << qMakePair(T("HyperDrive.Hotkey.Automation").arg(n), QString::number(n));
		whenForm->addRow(T("HyperDrive.Automation.Hotkey"), choice(keys, r->param, false, setParam));
		break;
	}
	case hd::TriggerKind::Timer:
		if (r->param.empty())
			r->param = "10";
		whenForm->addRow(T("HyperDrive.Automation.Minutes"),
				 choice(named({"1", "5", "10", "15", "30", "60"}), r->param, true, setParam));
		break;
	case hd::TriggerKind::BracketFocusChanged:
		whenForm->addRow(T("HyperDrive.Layout.Channel"),
				 choice(named(channels_, T("HyperDrive.Automation.AnyChannel")), r->param, true,
					setParam));
		break;
	default:
		break;
	}
	layout->addWidget(when);

	// Only if
	QGroupBox *onlyIf = new QGroupBox(T("HyperDrive.Automation.OnlyIf"), page);
	QVBoxLayout *ifLayout = new QVBoxLayout(onlyIf);
	Items conditionKinds;
	for (int k = 0; k < static_cast<int>(C::Count); k++)
		conditionKinds << qMakePair(kindText(C(k)), QString(hd::idOf(C(k))));
	for (size_t i = 0; i < r->conditions.size(); i++) {
		const hd::Condition &c = r->conditions[i];
		QHBoxLayout *row = new QHBoxLayout();
		row->addWidget(choice(conditionKinds, hd::idOf(c.kind), false,
				      [this, i](const std::string &v) {
					      hd::Condition &c = rule()->conditions[i];
					      C kind;
					      if (hd::parseId(v, kind) && kind != c.kind) {
						      c = {kind, ""};
						      later([this]() { buildEditor(); });
					      }
				      }),
			       1);
		auto set = [this, i](const std::string &v) {
			rule()->conditions[i].value = v;
		};
		switch (c.kind) {
		case C::SceneIs:
		case C::SceneIsNot:
			row->addWidget(choice(named(scenes_), c.value, true, set), 1);
			break;
		case C::BestOf:
			row->addWidget(choice(named({"1", "3", "5", "7"}), c.value, true, set), 1);
			break;
		case C::TeamHasGames:
			row->addWidget(choice(named({"0", "1", "2", "3", "4"}), c.value, true, set), 1);
			break;
		case C::RoundContains:
			row->addWidget(choice(named({"Grand Final", "Final", "Top 8"}), c.value, true, set), 1);
			break;
		case C::InstanceConnected:
			row->addWidget(
				choice(instanceItems(T("HyperDrive.Instance.Default")).mid(1), c.value, false, set), 1);
			break;
		default:
			row->addStretch(1);
			break;
		}
		row->addWidget(tool(QStringLiteral("✕"), T("HyperDrive.Settings.Remove"), page, [this, i]() {
			rule()->conditions.erase(rule()->conditions.begin() + static_cast<long>(i));
			later([this]() { buildEditor(); });
		}));
		ifLayout->addLayout(row);
	}
	if (r->conditions.empty())
		ifLayout->addWidget(help("HyperDrive.Automation.NoConditions", onlyIf));
	ifLayout->addWidget(push("HyperDrive.Automation.AddCondition", onlyIf,
				 [this]() {
					 rule()->conditions.push_back({});
					 later([this]() { buildEditor(); });
				 }),
			    0, Qt::AlignLeft);
	layout->addWidget(onlyIf);

	// Then
	QGroupBox *then = new QGroupBox(T("HyperDrive.Automation.Then"), page);
	QVBoxLayout *thenLayout = new QVBoxLayout(then);
	Items actionKinds;
	for (int k = 0; k < static_cast<int>(K::Count); k++)
		actionKinds << qMakePair(kindText(K(k)), QString(hd::idOf(K(k))));
	Items commands;
	for (const hd::NamedCommand &command : hd::namedCommands())
		commands << qMakePair(T(command.textKey).section(QStringLiteral(": "), -1), QString(command.id));
	Items boards{qMakePair(T("HyperDrive.Automation.EventScoreboard"), QString())};
	for (int n = 1; n <= 4; n++)
		boards << qMakePair(T("HyperDrive.Dock.ScoreboardN").arg(n), QString::number(n));
	for (size_t i = 0; i < r->actions.size(); i++) {
		const hd::Action &a = r->actions[i];
		QHBoxLayout *row = new QHBoxLayout();
		row->addWidget(new QLabel(QString::number(i + 1), then));
		row->addWidget(choice(actionKinds, hd::idOf(a.kind), false,
				      [this, i](const std::string &v) {
					      hd::Action &a = rule()->actions[i];
					      K kind;
					      if (hd::parseId(v, kind) && kind != a.kind) {
						      a = {kind, "", kind == K::Wait ? "1000" : ""};
						      later([this]() { buildEditor(); });
					      }
				      }),
			       1);
		auto target = [this, i, edited](const std::string &v) {
			rule()->actions[i].target = v;
			edited();
		};
		auto value = [this, i](const std::string &v) {
			rule()->actions[i].value = v;
		};
		switch (a.kind) {
		case K::SwitchScene:
			row->addWidget(choice(named(scenes_), a.target, true, target), 2);
			break;
		case K::ShowLayouts:
		case K::HideLayouts:
			row->addWidget(choice(named(displayTargets_), a.target, true, target), 2);
			break;
		case K::ShowSource:
		case K::HideSource:
			row->addWidget(choice(named(sources_), a.target, true, target), 1);
			row->addWidget(choice(named(scenes_, T("HyperDrive.Automation.ProgramScene")), a.value, true,
					      value),
				       1);
			break;
		case K::ToggleFilter: {
			row->addWidget(choice(named(sources_), a.target, true, target), 1);
			QComboBox *filter = choice({}, a.value, true, value);
			filter->lineEdit()->setPlaceholderText(T("HyperDrive.Automation.Filter"));
			row->addWidget(filter, 1);
			break;
		}
		case K::Command:
			row->addWidget(choice(commands, a.target, false, target), 1);
			row->addWidget(choice(boards, a.value, false, value), 1);
			break;
		case K::Wait: {
			QDoubleSpinBox *seconds = new QDoubleSpinBox(then);
			seconds->setRange(0, 600);
			seconds->setDecimals(1);
			seconds->setSingleStep(0.5);
			seconds->setSuffix(QStringLiteral(" s"));
			seconds->setValue(std::atoi(a.value.c_str()) / 1000.0);
			connect(seconds, &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
				rule()->actions[i].value = std::to_string(static_cast<int>(v * 1000 + 0.5));
			});
			row->addWidget(seconds, 2);
			break;
		}
		default:
			row->addStretch(2);
			break;
		}
		auto move = [this, i](int delta) {
			auto &actions = rule()->actions;
			size_t target = i + static_cast<size_t>(delta);
			if (target < actions.size()) {
				std::swap(actions[i], actions[target]);
				later([this]() {
					refreshRuleItem();
					buildEditor();
				});
			}
		};
		row->addWidget(tool(QStringLiteral("▲"), T("HyperDrive.Settings.Up"), page, [move]() { move(-1); }));
		row->addWidget(tool(QStringLiteral("▼"), T("HyperDrive.Settings.Down"), page, [move]() { move(1); }));
		row->addWidget(tool(QStringLiteral("✕"), T("HyperDrive.Settings.Remove"), page, [this, i]() {
			rule()->actions.erase(rule()->actions.begin() + static_cast<long>(i));
			later([this]() {
				refreshRuleItem();
				buildEditor();
			});
		}));
		thenLayout->addLayout(row);
	}
	thenLayout->addWidget(push("HyperDrive.Automation.AddAction", then,
				   [this]() {
					   rule()->actions.push_back({});
					   later([this]() {
						   refreshRuleItem();
						   buildEditor();
					   });
				   }),
			      0, Qt::AlignLeft);
	layout->addWidget(then);

	QHBoxLayout *buttons = new QHBoxLayout();
	buttons->addWidget(push("HyperDrive.Automation.Test", page, [this]() {
		if (Companion *companion = Companion::instance())
			companion->testRule(*rule());
	}));
	buttons->addWidget(push("HyperDrive.Automation.Duplicate", page, [this]() {
		hd::Rule copy = *rule();
		copy.name = T("HyperDrive.Automation.Copy")
				    .arg(q(copy.name.empty() ? "" : copy.name))
				    .trimmed()
				    .toStdString();
		working_.rules.insert(working_.rules.begin() + current_ + 1, copy);
		current_++;
		later([this]() { refreshRules(); });
	}));
	buttons->addStretch(1);
	buttons->addWidget(push("HyperDrive.Automation.Delete", page, [this]() {
		working_.rules.erase(working_.rules.begin() + current_);
		current_ = std::min(current_, static_cast<int>(working_.rules.size()) - 1);
		later([this]() { refreshRules(); });
	}));
	layout->addLayout(buttons);
	layout->addStretch(1);
	editor_->setWidget(page);
}

void SettingsDialog::exportRules()
{
	QString path = QFileDialog::getSaveFileName(this, T("HyperDrive.Automation.Export.Tip"),
						    QStringLiteral("hyperdrive-rules.json"),
						    QStringLiteral("JSON (*.json)"));
	if (path.isEmpty())
		return;
	// The group shown, or everything
	hd::CollectionConfig part;
	for (const hd::RuleGroup &g : working_.groups)
		if (allGroups_ || g.id == group_)
			part.groups.push_back(g);
	for (const hd::Rule &r : working_.rules)
		if (allGroups_ || r.group == group_)
			part.rules.push_back(r);
	QFile file(path);
	if (file.open(QIODevice::WriteOnly))
		file.write(QByteArray::fromStdString(hd::toJson(part)["automation"].dump(2)));
}

void SettingsDialog::importRules()
{
	QString path = QFileDialog::getOpenFileName(this, T("HyperDrive.Automation.Import.Tip"), QString(),
						    QStringLiteral("JSON (*.json)"));
	QFile file(path);
	if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
		return;
	nlohmann::json automation = nlohmann::json::parse(file.readAll().toStdString(), nullptr, false);
	hd::CollectionConfig part = hd::collectionConfigFromJson({{"automation", automation}});
	for (const hd::RuleGroup &g : part.groups) {
		if (std::none_of(working_.groups.begin(), working_.groups.end(),
				 [&](const hd::RuleGroup &other) { return other.id == g.id; }))
			working_.groups.push_back(g);
	}
	working_.rules.insert(working_.rules.end(), part.rules.begin(), part.rules.end());
	refreshGroups();
}

// ---------------------------------------------------------------------
// Hotkeys

QWidget *SettingsDialog::buildHotkeysTab()
{
	QWidget *tab = new QWidget(this);
	QFormLayout *form = new QFormLayout(tab);
	hotkeyInstance_ = new QComboBox(tab);
	form->addRow(T("HyperDrive.Settings.HotkeyInstance"), hotkeyInstance_);
	hotkeyScoreboard_ = new QSpinBox(tab);
	hotkeyScoreboard_->setRange(1, 32);
	form->addRow(T("HyperDrive.Settings.HotkeyScoreboard"), hotkeyScoreboard_);
	form->addRow(help("HyperDrive.Settings.Hotkeys.Help", tab));
	return tab;
}

// ---------------------------------------------------------------------

void SettingsDialog::load()
{
	Companion *companion = Companion::instance();
	if (!companion)
		return;

	scenes_.clear();
	for (const std::string &scene : Companion::sceneNames())
		scenes_ << q(scene);
	sources_.clear();
	obs_enum_sources(
		[](void *list, obs_source_t *source) {
			static_cast<QStringList *>(list)->append(QString::fromUtf8(obs_source_get_name(source)));
			return true;
		},
		&sources_);
	sources_.sort(Qt::CaseInsensitive);

	displayTargets_ = QStringList{QStringLiteral("all")};
	hd::DisplayState display;
	companion->hyperdrive()->client->withState([&](const nlohmann::json &state) {
		display = hd::displayState(state);
		channels_.clear();
		for (const std::string &channel : hd::bracketChannels(state))
			channels_ << q(channel);
	});
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

	const hd::GlobalConfig &settings = companion->settings();
	instances_->setRowCount(0);
	for (const hd::InstanceConfig &config : settings.instances)
		addInstanceRow(config);
	folder_->setText(q(settings.hyperdriveDir));
	alerts_->setChecked(settings.showAlerts);
	refreshStatus();

	working_ = companion->config();
	automationEnabled_->setChecked(working_.automationEnabled);
	current_ = working_.rules.empty() ? -1 : 0;
	allGroups_ = true;
	refreshGroups();
	log_->clear();
	log_->addItems(companion->automationLog());
	log_->scrollToBottom();

	hotkeyInstance_->clear();
	for (const auto &item : instanceItems(T("HyperDrive.Settings.FollowDock")))
		hotkeyInstance_->addItem(item.first, item.second);
	hotkeyInstance_->setCurrentIndex(std::max(0, hotkeyInstance_->findData(q(working_.hotkeyInstance))));
	hotkeyScoreboard_->setValue(working_.hotkeyScoreboard);
}

void SettingsDialog::save()
{
	Companion *companion = Companion::instance();
	if (!companion)
		return;

	hd::GlobalConfig settings = companion->settings();
	settings.instances = instanceRows();
	settings.hyperdriveDir = folder_->text().trimmed().toStdString();
	settings.showAlerts = alerts_->isChecked();
	companion->setSettings(settings);
	// New instances got their ids
	const std::vector<hd::InstanceConfig> &saved = companion->settings().instances;
	for (int row = 0; row < instances_->rowCount() && static_cast<size_t>(row) < saved.size(); row++)
		instances_->item(row, 1)->setData(Qt::UserRole, q(saved[static_cast<size_t>(row)].id));
	refreshStatus();

	working_.automationEnabled = automationEnabled_->isChecked();
	working_.hotkeyInstance = hotkeyInstance_->currentData().toString().toStdString();
	working_.hotkeyScoreboard = hotkeyScoreboard_->value();
	companion->setConfig(working_);
}
