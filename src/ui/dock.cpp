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
#include "dock.hpp"

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "companion.hpp"
#include "core/commands.hpp"
#include "core/state.hpp"
#include "settings-dialog.hpp"
#include "style.hpp"
#include "text.hpp"

using nlohmann::json;

namespace {

QString q(const std::string &s)
{
	return QString::fromStdString(s);
}

// The tabs' line icons (24x24 SVG paths)
const char *kIcons[] = {
	"<rect x='3' y='5' width='18' height='14' rx='2'/><path d='M12 5v14M7 10v4M17 10v4'/>",
	"<path d='M4 6h16M4 12h16M4 18h10'/>",
	"<circle cx='9' cy='8' r='3'/><circle cx='17' cy='9' r='2.5'/><path d='M3 20c0-3.3 2.7-6 6-6s6 2.7 6 6M15 14.5c3 0 6 2 6 5.5'/>",
	"<path d='M3 6l6-3 6 3 6-3v15l-6 3-6-3-6 3z'/><path d='M9 3v15M15 6v15'/>",
	"<path d='M3 5h5v6H3M3 13h5v6H3M8 8h4v8H8M12 12h9'/>",
	"<path d='M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z'/><circle cx='12' cy='12' r='3'/>",
};

const char *kStyle = R"(
#hdTeam, #hdCard { background: palette(base); border: 1px solid palette(mid); border-radius: 8px; }
#hdTeam[winner="true"] { border: 1px solid palette(highlight); }
#hdCard { padding: 8px 10px; }
#hdName { font-size: 15px; font-weight: 600; }
#hdDetail, #hdStatus, #hdFooter { color: palette(placeholder-text); font-size: 11px; }
#hdDetail[state="point"] { color: #ff8a3d; font-weight: 600; }
#hdDetail[state="won"] { color: palette(highlight); font-weight: 600; }
#hdScore { font-size: 28px; font-weight: 700; min-width: 38px; }
#hdStep { min-width: 30px; min-height: 30px; font-size: 16px; font-weight: 600; }
#hdRound { font-weight: 700; letter-spacing: 1px; }
#hdPill { border: 1px solid palette(mid); border-radius: 9px; padding: 1px 8px; color: palette(placeholder-text); font-size: 11px; }
#hdNav { border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 4px 0; font-size: 11px; }
#hdNav:checked { border-bottom: 2px solid palette(highlight); }
#hdSegment { padding: 3px 8px; font-weight: 600; }
#hdToast { background: #c92f35; color: white; border-radius: 6px; padding: 8px 10px; }
)";

QPushButton *button(const char *textKey, QWidget *parent, const std::function<void()> &onClick)
{
	QPushButton *b = new QPushButton(T(textKey), parent);
	QObject::connect(b, &QPushButton::clicked, parent, onClick);
	return b;
}

QToolButton *smallButton(const QString &text, const char *tipKey, QWidget *parent, const std::function<void()> &onClick,
			 const char *name = nullptr)
{
	QToolButton *b = new QToolButton(parent);
	b->setText(text);
	b->setToolTip(T(tipKey));
	b->setMinimumWidth(28);
	if (name)
		b->setObjectName(QString::fromLatin1(name));
	QObject::connect(b, &QToolButton::clicked, parent, onClick);
	return b;
}

QLabel *label(const QString &text, const char *name, QWidget *parent)
{
	QLabel *l = new QLabel(text, parent);
	l->setObjectName(QString::fromLatin1(name));
	return l;
}

/** Applies a changed dynamic property to the style */
void setState(QWidget *widget, const char *property, const QVariant &value)
{
	if (widget->property(property) == value)
		return;
	widget->setProperty(property, value);
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

} // namespace

HyperDriveDock::HyperDriveDock(QWidget *parent) : QWidget(parent)
{
	setStyleSheet(QString::fromLatin1(kStyle));
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(6);

	// Which HyperDrive, which scoreboard
	QHBoxLayout *header = new QHBoxLayout();
	instancePicker_ = new QComboBox(this);
	instancePicker_->setToolTip(T("HyperDrive.Dock.Instance.Tip"));
	instancePicker_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	instancePicker_->setMinimumContentsLength(6);
	connect(instancePicker_, &QComboBox::activated, this, [this]() {
		if (Companion *companion = Companion::instance())
			companion->setDockInstance(instancePicker_->currentData().toString().toStdString());
	});
	header->addWidget(instancePicker_, 1);
	scoreboards_ = new QHBoxLayout();
	scoreboards_->setSpacing(0);
	header->addLayout(scoreboards_);
	scoreboardButtons_ = new QButtonGroup(this);
	connect(scoreboardButtons_, &QButtonGroup::idClicked, this, [this](int sb) {
		scoreboard_ = sb;
		refresh();
	});
	layout->addLayout(header);
	status_ = label(QString(), "hdStatus", this);
	status_->setTextFormat(Qt::RichText);
	layout->addWidget(status_);

	// The tabs: labelled icons, which fit a narrow dock
	QHBoxLayout *nav = new QHBoxLayout();
	nav->setSpacing(0);
	pages_ = new QStackedWidget(this);
	QButtonGroup *tabs = new QButtonGroup(this);
	const char *names[] = {"HyperDrive.Dock.Tab.Score",   "HyperDrive.Dock.Tab.Queue",
			       "HyperDrive.Dock.Tab.Battle",  "HyperDrive.Dock.Tab.Strike",
			       "HyperDrive.Dock.Tab.Bracket", "HyperDrive.Dock.Tab.Display"};
	QWidget *pages[] = {buildScoreTab(),  buildQueueTab(),   buildBattleTab(),
			    buildStrikeTab(), buildBracketTab(), buildDisplayTab()};
	for (int i = 0; i < 6; i++) {
		QToolButton *tab = new QToolButton(this);
		tab->setObjectName(QStringLiteral("hdNav"));
		tab->setText(T(names[i]));
		tab->setIcon(style::icon(kIcons[i], palette().color(QPalette::WindowText)));
		tab->setIconSize(QSize(18, 18));
		tab->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		tab->setCheckable(true);
		tab->setChecked(i == 0);
		tab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		tabs->addButton(tab, i);
		nav->addWidget(tab);
		pages_->addWidget(pages[i]);
	}
	connect(tabs, &QButtonGroup::idClicked, pages_, &QStackedWidget::setCurrentIndex);
	layout->addLayout(nav);
	layout->addWidget(pages_, 1);

	// A refused command, for a moment
	toast_ = label(QString(), "hdToast", this);
	toast_->setWordWrap(true);
	toast_->hide();
	toastTimer_ = new QTimer(this);
	toastTimer_->setSingleShot(true);
	connect(toastTimer_, &QTimer::timeout, toast_, &QLabel::hide);
	layout->addWidget(toast_);

	// What the automation will do
	QHBoxLayout *footer = new QHBoxLayout();
	automation_ = label(QString(), "hdFooter", this);
	automation_->setWordWrap(true);
	footer->addWidget(automation_, 1);
	QPushButton *edit = button("HyperDrive.Dock.EditRules", this,
				   []() { SettingsDialog::showDialog(SettingsDialog::Automation); });
	edit->setFlat(true);
	footer->addWidget(edit);
	layout->addLayout(footer);

	if (Companion *companion = Companion::instance()) {
		connect(companion, &Companion::stateChanged, this, [this](const QString &id) {
			if (id.toStdString() == instance_)
				refresh();
		});
		connect(companion, &Companion::statusChanged, this, &HyperDriveDock::refreshStatus);
		connect(companion, &Companion::settingsChanged, this, &HyperDriveDock::refresh);
		connect(companion, &Companion::configChanged, this, &HyperDriveDock::refreshAutomation);
		connect(companion, &Companion::commandFailed, this, &HyperDriveDock::showError);
		connect(companion, &Companion::automationRan, this, &HyperDriveDock::refreshAutomation);
	}
	refresh();
}

void HyperDriveDock::send(const hd::Request &request)
{
	if (Companion *companion = Companion::instance())
		companion->command(request, instance_);
}

bool HyperDriveDock::confirm(const QString &question)
{
	return QMessageBox::question(this, T("HyperDrive.Name"), question) == QMessageBox::Yes;
}

void HyperDriveDock::showError(const QString &message)
{
	toast_->setText(message);
	toast_->show();
	toastTimer_->start(5000);
}

QWidget *HyperDriveDock::buildScoreTab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	layout->setContentsMargins(0, 6, 0, 0);
	QHBoxLayout *head = new QHBoxLayout();
	round_ = label(QString(), "hdRound", tab);
	pill_ = label(QString(), "hdPill", tab);
	head->addWidget(round_, 1);
	head->addWidget(pill_);
	layout->addLayout(head);

	for (int t = 0; t < 2; t++) {
		int team = t + 1;
		if (t == 1) {
			QHBoxLayout *swap = new QHBoxLayout();
			swap->addStretch(1);
			QToolButton *b = smallButton(QStringLiteral("⇅ ") + T("HyperDrive.Dock.Swap"),
						     "HyperDrive.Dock.Swap", tab,
						     [this]() { send(hd::scoreboard::swapTeams(scoreboard_)); });
			b->setAutoRaise(true);
			swap->addWidget(b);
			swap->addStretch(1);
			layout->addLayout(swap);
		}
		TeamRow &row = teams_[t];
		row.card = new QFrame(tab);
		row.card->setObjectName(QStringLiteral("hdTeam"));
		QHBoxLayout *card = new QHBoxLayout(row.card);
		card->setContentsMargins(0, 0, 8, 0);
		row.color = new QToolButton(row.card);
		row.color->setToolTip(T("HyperDrive.Dock.TeamColor"));
		row.color->setFixedWidth(8);
		row.color->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
		connect(row.color, &QToolButton::clicked, this, [this, team]() { pickColor(team); });
		card->addWidget(row.color);
		QVBoxLayout *names = new QVBoxLayout();
		names->setContentsMargins(4, 8, 0, 8);
		names->setSpacing(0);
		row.name = label(QString(), "hdName", row.card);
		row.name->setTextInteractionFlags(Qt::TextSelectableByMouse);
		row.detail = label(QString(), "hdDetail", row.card);
		names->addWidget(row.name);
		names->addWidget(row.detail);
		card->addLayout(names, 1);
		card->addWidget(smallButton(
			QStringLiteral("−"), "HyperDrive.Dock.ScoreDown", row.card,
			[this, team]() { send(hd::scoreboard::scoreDown(scoreboard_, team)); }, "hdStep"));
		row.score = label(QStringLiteral("0"), "hdScore", row.card);
		row.score->setAlignment(Qt::AlignCenter);
		card->addWidget(row.score);
		card->addWidget(smallButton(
			QStringLiteral("+"), "HyperDrive.Dock.ScoreUp", row.card,
			[this, team]() { send(hd::scoreboard::scoreUp(scoreboard_, team)); }, "hdStep"));
		layout->addWidget(row.card);
	}

	QHBoxLayout *actions = new QHBoxLayout();
	QToolButton *reset = new QToolButton(tab);
	reset->setText(T("HyperDrive.Dock.Reset"));
	reset->setPopupMode(QToolButton::InstantPopup);
	QMenu *menu = new QMenu(reset);
	using Kind = hd::scoreboard::ResetKind;
	struct ResetItem {
		const char *key;
		Kind kind;
		bool ask;
	};
	for (const ResetItem &item : {ResetItem{"HyperDrive.Dock.Reset.Scores", Kind::Scores, false},
				      ResetItem{"HyperDrive.Dock.Reset.Match", Kind::Match, false},
				      ResetItem{"HyperDrive.Dock.Reset.Players", Kind::Players, true},
				      ResetItem{"HyperDrive.Dock.Reset.All", Kind::All, true}}) {
		menu->addAction(T(item.key), this, [this, item]() {
			if (!item.ask || confirm(T("HyperDrive.Dock.Reset.Confirm").arg(T(item.key))))
				send(hd::scoreboard::reset(scoreboard_, item.kind));
		});
	}
	reset->setMenu(menu);
	actions->addWidget(reset);
	actions->addWidget(button("HyperDrive.Dock.LoadNext", tab,
				  [this]() { send(hd::scoreboard::loadNextQueued(scoreboard_)); }));
	actions->addStretch(1);
	layout->addLayout(actions);
	layout->addStretch(1);
	return tab;
}

QWidget *HyperDriveDock::buildQueueTab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	layout->setContentsMargins(0, 6, 0, 0);
	nextSet_ = label(QString(), "hdCard", tab);
	nextSet_->setWordWrap(true);
	layout->addWidget(nextSet_);
	QPushButton *load = button("HyperDrive.Dock.LoadNext", tab,
				   [this]() { send(hd::scoreboard::loadNextQueued(scoreboard_)); });
	load->setDefault(true);
	layout->addWidget(load);
	layout->addWidget(button("HyperDrive.Dock.PullStream", tab,
				 [this]() { send(hd::scoreboard::pullStreamSet(scoreboard_)); }));
	layout->addWidget(button("HyperDrive.Dock.OpenSetSelector", tab,
				 [this]() { send(hd::scoreboard::openSetSelector(scoreboard_)); }));
	layout->addStretch(1);
	return tab;
}

QWidget *HyperDriveDock::buildBattleTab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	for (int t = 0; t < 2; t++) {
		int team = t + 1;
		BattleRow &row = battle_[t];
		QGroupBox *box = new QGroupBox(T("HyperDrive.Dock.Team").arg(team), tab);
		QGridLayout *grid = new QGridLayout(box);
		row.name = new QLabel(box);
		row.players = new QComboBox(box);
		row.players->setToolTip(T("HyperDrive.Dock.ActivePlayer"));
		connect(row.players, &QComboBox::activated, this, [this, team, t]() {
			int player = battle_[t].players->currentData().toInt();
			if (player > 0)
				send(hd::teamBattle::setActive(team, player));
		});
		grid->addWidget(row.name, 0, 0, 1, 4);
		grid->addWidget(row.players, 1, 0);
		grid->addWidget(smallButton(QStringLiteral("−"), "HyperDrive.Dock.BattleScoreDown", box,
					    [this, team]() { send(hd::teamBattle::scoreDown(team)); }),
				1, 1);
		grid->addWidget(smallButton(QStringLiteral("+"), "HyperDrive.Dock.BattleScoreUp", box,
					    [this, team]() { send(hd::teamBattle::scoreUp(team)); }),
				1, 2);
		grid->addWidget(smallButton(QStringLiteral("»"), "HyperDrive.Dock.NextPlayer", box,
					    [this, team]() { send(hd::teamBattle::nextPlayer(team)); }),
				1, 3);
		grid->setColumnStretch(0, 1);
		layout->addWidget(box);
	}
	battleInfo_ = new QLabel(tab);
	layout->addWidget(battleInfo_);
	QHBoxLayout *resets = new QHBoxLayout();
	resets->addWidget(button("HyperDrive.Dock.BattleResetStocks", tab, [this]() {
		if (confirm(T("HyperDrive.Dock.BattleResetStocks.Confirm")))
			send(hd::teamBattle::resetStocks());
	}));
	resets->addWidget(button("HyperDrive.Dock.BattleReset", tab, [this]() {
		if (confirm(T("HyperDrive.Dock.BattleReset.Confirm")))
			send(hd::teamBattle::reset());
	}));
	layout->addLayout(resets);
	layout->addStretch(1);
	return tab;
}

QWidget *HyperDriveDock::buildStrikeTab()
{
	using C = hd::stageStrike::Command;
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	layout->setContentsMargins(0, 6, 0, 0);
	strikeInfo_ = label(QString(), "hdCard", tab);
	strikeInfo_->setWordWrap(true);
	layout->addWidget(strikeInfo_);

	QFormLayout *winners = new QFormLayout();
	QHBoxLayout *rps = new QHBoxLayout();
	QHBoxLayout *game = new QHBoxLayout();
	for (int t = 0; t < 2; t++) {
		int team = t + 1;
		rps_[t] = new QPushButton(tab);
		connect(rps_[t], &QPushButton::clicked, this,
			[this, team]() { send(hd::stageStrike::run(scoreboard_, C::RpsWin, team)); });
		rps->addWidget(rps_[t]);
		gameWin_[t] = new QPushButton(tab);
		connect(gameWin_[t], &QPushButton::clicked, this,
			[this, team]() { send(hd::stageStrike::run(scoreboard_, C::GameWin, team)); });
		game->addWidget(gameWin_[t]);
	}
	winners->addRow(T("HyperDrive.Dock.RpsWinner"), rps);
	winners->addRow(T("HyperDrive.Dock.GameWinner"), game);
	layout->addLayout(winners);

	QHBoxLayout *history = new QHBoxLayout();
	history->addWidget(
		button("HyperDrive.Dock.Undo", tab, [this]() { send(hd::stageStrike::run(scoreboard_, C::Undo)); }));
	history->addWidget(
		button("HyperDrive.Dock.Redo", tab, [this]() { send(hd::stageStrike::run(scoreboard_, C::Redo)); }));
	history->addWidget(button("HyperDrive.Dock.StrikeReset", tab, [this]() {
		if (confirm(T("HyperDrive.Dock.StrikeReset.Confirm")))
			send(hd::stageStrike::run(scoreboard_, C::Reset));
	}));
	layout->addLayout(history);
	layout->addStretch(1);
	return tab;
}

QWidget *HyperDriveDock::buildBracketTab()
{
	using M = hd::bracketFocus::Mode;
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	layout->setContentsMargins(0, 6, 0, 0);
	QFormLayout *form = new QFormLayout();
	bracketChannel_ = new QComboBox(tab);
	bracketChannel_->addItem(QStringLiteral("main"));
	connect(bracketChannel_, &QComboBox::currentIndexChanged, this, [this]() { refresh(); });
	form->addRow(T("HyperDrive.Dock.Channel"), bracketChannel_);
	layout->addLayout(form);

	auto focus = [this](M mode) {
		send(hd::bracketFocus::set(mode, bracketChannel_->currentText().toStdString(), scoreboard_));
	};
	QHBoxLayout *rounds = new QHBoxLayout();
	rounds->addWidget(smallButton(
		QStringLiteral("‹"), "HyperDrive.Dock.Bracket.Previous", tab, [focus]() { focus(M::PreviousRound); },
		"hdStep"));
	bracketInfo_ = label(QString(), "hdRound", tab);
	bracketInfo_->setAlignment(Qt::AlignCenter);
	bracketInfo_->setWordWrap(true);
	rounds->addWidget(bracketInfo_, 1);
	rounds->addWidget(smallButton(
		QStringLiteral("›"), "HyperDrive.Dock.Bracket.Next", tab, [focus]() { focus(M::NextRound); },
		"hdStep"));
	layout->addLayout(rounds);
	QGridLayout *grid = new QGridLayout();
	grid->addWidget(button("HyperDrive.Dock.Bracket.All", tab, [focus]() { focus(M::All); }), 0, 0);
	grid->addWidget(button("HyperDrive.Dock.Bracket.Follow", tab, [focus]() { focus(M::Follow); }), 0, 1);
	grid->addWidget(button("HyperDrive.Dock.Bracket.Tour", tab, [focus]() { focus(M::Tour); }), 1, 0, 1, 2);
	layout->addLayout(grid);
	layout->addStretch(1);
	return tab;
}

QWidget *HyperDriveDock::buildDisplayTab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	displayList_ = new QListWidget(tab);
	displayList_->setToolTip(T("HyperDrive.Dock.Display.Tip"));
	connect(displayList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
		auto target = hd::DisplayTarget::parse(item->data(Qt::UserRole).toString().toStdString());
		if (target)
			send(hd::display::set(*target, item->checkState() == Qt::Checked ? hd::display::Action::Show
											 : hd::display::Action::Hide));
	});
	layout->addWidget(displayList_, 1);
	QHBoxLayout *all = new QHBoxLayout();
	all->addWidget(button("HyperDrive.Dock.ShowAll", tab,
			      [this]() { send(hd::display::set(hd::DisplayTarget(), hd::display::Action::Show)); }));
	all->addWidget(button("HyperDrive.Dock.HideAll", tab,
			      [this]() { send(hd::display::set(hd::DisplayTarget(), hd::display::Action::Hide)); }));
	layout->addLayout(all);
	return tab;
}

void HyperDriveDock::pickColor(int team)
{
	QColor initial = teams_[team - 1].color->property("hdColor").value<QColor>();
	QColor color = QColorDialog::getColor(initial.isValid() ? initial : QColor(Qt::white), this,
					      T("HyperDrive.Dock.TeamColor"));
	if (color.isValid())
		send(hd::scoreboard::teamColor(scoreboard_, team, color.name().toStdString()));
}

void HyperDriveDock::refreshInstances()
{
	Companion *companion = Companion::instance();
	QSignalBlocker block(instancePicker_);
	instancePicker_->clear();
	for (const auto &drive : companion->hyperdrives())
		instancePicker_->addItem(style::swatch(q(drive->config.color), 12), q(drive->config.name),
					 q(drive->config.id));
	instancePicker_->setCurrentIndex(std::max(0, instancePicker_->findData(q(instance_))));
}

void HyperDriveDock::refreshStatus()
{
	Companion *companion = Companion::instance();
	if (!companion)
		return;
	auto drive = companion->hyperdrive(instance_);
	hd::Address address = drive->client->address();
	status_->setText(style::status(drive->client->status(), q(address.host) + ":" + QString::number(address.port)));
}

void HyperDriveDock::refreshAutomation()
{
	Companion *companion = Companion::instance();
	if (!companion)
		return;
	const hd::CollectionConfig &config = companion->config();
	int armed = static_cast<int>(std::count_if(config.rules.begin(), config.rules.end(),
						   [&](const hd::Rule &r) { return hd::ruleArmed(config, r); }));
	QString text = !config.automationEnabled ? T("HyperDrive.Dock.AutomationOff")
						 : T("HyperDrive.Dock.AutomationArmed").arg(armed);
	if (!companion->automationLog().isEmpty())
		text += QStringLiteral("\n") + companion->automationLog().last();
	automation_->setText(QStringLiteral("⚡ ") + text);
}

void HyperDriveDock::refresh()
{
	Companion *companion = Companion::instance();
	if (!companion)
		return;
	instance_ = companion->dockInstance();
	refreshInstances();
	refreshStatus();
	refreshAutomation();
	companion->hyperdrive(instance_)->client->withState([this](const json &state) {
		refreshScoreboards(state);
		refreshScoreboard(state);
		refreshBattle(state);
		refreshStrike(state);
		refreshBracket(state);
		refreshDisplay(state);
	});
}

void HyperDriveDock::refreshScoreboards(const json &state)
{
	// The scoreboards HyperDrive has, as a segmented control
	std::vector<int> numbers = hd::scoreboardNumbers(state);
	if (numbers.empty())
		numbers.push_back(1);
	if (std::find(numbers.begin(), numbers.end(), scoreboard_) == numbers.end())
		scoreboard_ = numbers.front();
	QList<QAbstractButton *> buttons = scoreboardButtons_->buttons();
	bool same = static_cast<size_t>(buttons.size()) == numbers.size();
	for (size_t i = 0; same && i < numbers.size(); i++)
		same = scoreboardButtons_->id(buttons[static_cast<int>(i)]) == numbers[i];
	if (!same) {
		for (QAbstractButton *b : buttons)
			delete b;
		for (int n : numbers) {
			QToolButton *b = new QToolButton(this);
			b->setObjectName(QStringLiteral("hdSegment"));
			b->setText(QStringLiteral("SB%1").arg(n));
			b->setToolTip(T("HyperDrive.Dock.ScoreboardN").arg(n));
			b->setCheckable(true);
			scoreboardButtons_->addButton(b, n);
			scoreboards_->addWidget(b);
		}
	}
	if (QAbstractButton *b = scoreboardButtons_->button(scoreboard_))
		b->setChecked(true);
	// One scoreboard: nothing to pick
	for (QAbstractButton *b : scoreboardButtons_->buttons())
		b->setVisible(numbers.size() > 1);
}

void HyperDriveDock::refreshScoreboard(const json &state)
{
	int sb = scoreboard_;
	int bestOf = hd::bestOf(state, sb);
	int wins = hd::winsNeeded(bestOf);
	int played = 0;
	int winner = -1;
	std::optional<hd::TeamInfo> teams[2] = {hd::scoreboardTeam(state, sb, 1), hd::scoreboardTeam(state, sb, 2)};
	for (int t = 0; t < 2; t++) {
		int score = teams[t] ? teams[t]->score : 0;
		played += score;
		if (wins > 0 && score >= wins)
			winner = t;
	}
	for (int t = 0; t < 2; t++) {
		const auto &team = teams[t];
		TeamRow &row = teams_[t];
		QString name = team && !team->name.empty() ? q(team->name) : T("HyperDrive.Dock.Team").arg(t + 1);
		if (team && team->losers)
			name += QStringLiteral(" [L]");
		row.name->setText(name);
		int score = team ? team->score : 0;
		row.score->setText(QString::number(score));
		std::string color = team ? team->color : "";
		row.color->setStyleSheet(
			QStringLiteral("QToolButton { border: none; border-radius: 0; background: %1; }")
				.arg(color.empty() ? QStringLiteral("palette(mid)") : q(color)));
		row.color->setProperty("hdColor", color.empty() ? QColor() : QColor(q(color)));
		bool point = winner < 0 && wins > 1 && score == wins - 1;
		row.detail->setText(winner == t ? T("HyperDrive.Dock.WinsSet")
				    : point     ? T("HyperDrive.Dock.MatchPoint")
						: QString());
		row.detail->setVisible(!row.detail->text().isEmpty());
		setState(row.detail, "state", winner == t ? "won" : point ? "point" : "");
		setState(row.card, "winner", winner == t);
		rps_[t]->setText(name);
		gameWin_[t]->setText(name);
	}

	std::string match = hd::matchName(state, sb);
	round_->setText(q(match).toUpper());
	QString progress = winner >= 0 ? T("HyperDrive.Dock.SetOver") : T("HyperDrive.Dock.GameN").arg(played + 1);
	pill_->setText(bestOf > 0 ? QStringLiteral("Bo%1 · %2").arg(bestOf).arg(progress) : progress);

	auto next = hd::nextQueuedSet(state, sb);
	nextSet_->setText(next ? T("HyperDrive.Dock.NextSet").arg(q(*next)) : T("HyperDrive.Dock.NoNextSet"));
}

void HyperDriveDock::refreshBattle(const json &state)
{
	bool firstTo = false;
	int battleValue = 0;
	for (int t = 0; t < 2; t++) {
		BattleRow &row = battle_[t];
		auto team = hd::teamBattleTeam(state, t + 1);
		QSignalBlocker block(row.players);
		if (!team) {
			row.players->clear();
			row.name->setText(T("HyperDrive.Dock.NoBattle"));
			continue;
		}
		firstTo = team->firstTo;
		battleValue = team->battleValue;
		row.name->setText(
			QStringLiteral("<b>%1</b> — %2")
				.arg((team->name.empty() ? T("HyperDrive.Dock.Team").arg(t + 1) : q(team->name))
					     .toHtmlEscaped(),
				     T(team->firstTo ? "HyperDrive.Dock.Games" : "HyperDrive.Dock.Stocks")
					     .arg(team->total)));
		QStringList labels;
		int activeIndex = -1;
		for (const hd::BattlePlayer &player : team->players) {
			QString label = QStringLiteral("%1 (%2)")
						.arg(player.name.empty()
							     ? T("HyperDrive.Dock.PlayerN").arg(player.number)
							     : q(player.name))
						.arg(player.value);
			if (player.dead)
				label += QStringLiteral(" ✕");
			if (player.number == team->active)
				activeIndex = static_cast<int>(labels.size());
			labels << label;
		}
		// Rebuilt only when it changed, so an open list stays open
		bool same = row.players->count() == labels.size();
		for (int i = 0; same && i < row.players->count(); i++)
			same = row.players->itemText(i) == labels[i];
		if (!same) {
			row.players->clear();
			for (int i = 0; i < labels.size(); i++)
				row.players->addItem(labels[i], team->players[static_cast<size_t>(i)].number);
		}
		row.players->setCurrentIndex(activeIndex);
	}
	battleInfo_->setText(
		battleValue > 0 ? T(firstTo ? "HyperDrive.Dock.FirstTo" : "HyperDrive.Dock.StockPool").arg(battleValue)
				: QString());
}

void HyperDriveDock::refreshBracket(const json &state)
{
	std::vector<std::string> channels = hd::bracketChannels(state);
	bool same = static_cast<size_t>(bracketChannel_->count()) == channels.size();
	for (size_t i = 0; same && i < channels.size(); i++)
		same = bracketChannel_->itemText(static_cast<int>(i)).toStdString() == channels[i];
	if (!same) {
		QSignalBlocker block(bracketChannel_);
		QString current = bracketChannel_->currentText();
		bracketChannel_->clear();
		for (const std::string &channel : channels)
			bracketChannel_->addItem(q(channel));
		int index = bracketChannel_->findText(current);
		bracketChannel_->setCurrentIndex(index >= 0 ? index : 0);
	}
	std::string label = hd::bracketFocusLabel(state, bracketChannel_->currentText().toStdString());
	bracketInfo_->setText(label.empty() ? QString() : T("HyperDrive.Dock.Showing").arg(q(label)));
}

void HyperDriveDock::refreshDisplay(const json &state)
{
	hd::DisplayState display = hd::displayState(state);
	QSignalBlocker block(displayList_);

	std::vector<std::pair<QString, bool>> entries;
	for (const auto &folder : display.folders)
		entries.emplace_back(QStringLiteral("folder:") + q(folder.first), folder.second);
	for (const auto &group : display.groups)
		entries.emplace_back(QStringLiteral("group:") + q(group.first), group.second);

	// The same layouts: only their states change, keeping the scrolling and selection
	bool same = !entries.empty() && displayList_->count() == static_cast<int>(entries.size());
	for (int i = 0; same && i < displayList_->count(); i++)
		same = displayList_->item(i)->data(Qt::UserRole).toString() == entries[static_cast<size_t>(i)].first;
	if (same) {
		for (int i = 0; i < displayList_->count(); i++)
			displayList_->item(i)->setCheckState(entries[static_cast<size_t>(i)].second ? Qt::Checked
												    : Qt::Unchecked);
		return;
	}

	QString selected = displayList_->currentItem() ? displayList_->currentItem()->data(Qt::UserRole).toString()
						       : QString();
	displayList_->clear();
	for (const auto &entry : entries) {
		QString name = entry.first.section(QLatin1Char(':'), 1);
		bool folder = entry.first.startsWith(QStringLiteral("folder:"));
		QListWidgetItem *item = new QListWidgetItem(
			T(folder ? "HyperDrive.Dock.Folder" : "HyperDrive.Dock.Group").arg(name), displayList_);
		item->setData(Qt::UserRole, entry.first);
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
		item->setCheckState(entry.second ? Qt::Checked : Qt::Unchecked);
		if (entry.first == selected)
			displayList_->setCurrentItem(item);
	}
	if (entries.empty()) {
		QListWidgetItem *item = new QListWidgetItem(T("HyperDrive.Dock.NoDisplay"), displayList_);
		item->setFlags(Qt::NoItemFlags);
	}
}

void HyperDriveDock::refreshStrike(const json &state)
{
	hd::StrikeInfo strike = hd::stageStrikeInfo(state, scoreboard_);
	QString text = T("HyperDrive.Dock.GameN").arg(strike.game + 1);
	if (strike.stageSelected)
		text += QStringLiteral(" · ") + T("HyperDrive.Dock.Stage").arg(q(strike.stage));
	else if (strike.active)
		text += QStringLiteral(" · ") + T("HyperDrive.Dock.Striking");
	strikeInfo_->setText(text);
}
