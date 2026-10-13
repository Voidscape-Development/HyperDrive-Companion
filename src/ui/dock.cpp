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

#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "companion.hpp"
#include "core/commands.hpp"
#include "core/state.hpp"
#include "text.hpp"

using nlohmann::json;

namespace {

QString q(const std::string &s)
{
	return QString::fromStdString(s);
}

QIcon swatch(const std::string &color)
{
	QPixmap pixmap(16, 16);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(QColor(128, 128, 128), 1));
	painter.setBrush(color.empty() ? QBrush(Qt::NoBrush) : QBrush(QColor(q(color))));
	painter.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
	return QIcon(pixmap);
}

QPushButton *button(const char *textKey, QWidget *parent, const std::function<void()> &onClick)
{
	QPushButton *b = new QPushButton(T(textKey), parent);
	QObject::connect(b, &QPushButton::clicked, parent, onClick);
	return b;
}

QToolButton *smallButton(const QString &text, const char *tipKey, QWidget *parent, const std::function<void()> &onClick)
{
	QToolButton *b = new QToolButton(parent);
	b->setText(text);
	b->setToolTip(T(tipKey));
	b->setMinimumWidth(28);
	QObject::connect(b, &QToolButton::clicked, parent, onClick);
	return b;
}

} // namespace

HyperDriveDock::HyperDriveDock(QWidget *parent) : QWidget(parent)
{
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);

	QHBoxLayout *header = new QHBoxLayout();
	status_ = new QLabel(this);
	status_->setTextFormat(Qt::RichText);
	scoreboardPicker_ = new QComboBox(this);
	scoreboardPicker_->setToolTip(T("HyperDrive.Dock.Scoreboard.Tip"));
	header->addWidget(status_, 1);
	header->addWidget(scoreboardPicker_);
	layout->addLayout(header);

	error_ = new QLabel(this);
	error_->setWordWrap(true);
	error_->setStyleSheet(QStringLiteral("QLabel { color: #e85d5d; }"));
	error_->hide();
	errorTimer_ = new QTimer(this);
	errorTimer_->setSingleShot(true);
	connect(errorTimer_, &QTimer::timeout, error_, &QLabel::hide);
	layout->addWidget(error_);

	QTabWidget *tabs = new QTabWidget(this);
	tabs->setDocumentMode(true);
	tabs->addTab(buildScoreTab(), T("HyperDrive.Dock.Tab.Score"));
	tabs->addTab(buildQueueTab(), T("HyperDrive.Dock.Tab.Queue"));
	tabs->addTab(buildBattleTab(), T("HyperDrive.Dock.Tab.Battle"));
	tabs->addTab(buildStrikeTab(), T("HyperDrive.Dock.Tab.Strike"));
	tabs->addTab(buildBracketTab(), T("HyperDrive.Dock.Tab.Bracket"));
	tabs->addTab(buildDisplayTab(), T("HyperDrive.Dock.Tab.Display"));
	layout->addWidget(tabs, 1);

	connect(scoreboardPicker_, &QComboBox::currentIndexChanged, this, [this]() { refresh(); });

	if (Companion *companion = Companion::instance()) {
		connect(companion, &Companion::stateChanged, this, &HyperDriveDock::refresh);
		connect(companion, &Companion::statusChanged, this, &HyperDriveDock::refreshStatus);
		connect(companion, &Companion::settingsChanged, this, &HyperDriveDock::refreshStatus);
		connect(companion, &Companion::commandFailed, this, &HyperDriveDock::showError);
	}
	refresh();
}

int HyperDriveDock::scoreboard() const
{
	int sb = scoreboardPicker_->currentData().toInt();
	return sb > 0 ? sb : 1;
}

void HyperDriveDock::send(const hd::Request &request)
{
	if (Companion *companion = Companion::instance())
		companion->command(request);
}

bool HyperDriveDock::confirm(const QString &question)
{
	return QMessageBox::question(this, T("HyperDrive.Name"), question) == QMessageBox::Yes;
}

void HyperDriveDock::showError(const QString &message)
{
	error_->setText(message);
	error_->show();
	errorTimer_->start(6000);
}

QWidget *HyperDriveDock::buildScoreTab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	QGridLayout *grid = new QGridLayout();
	for (int t = 0; t < 2; t++) {
		int team = t + 1;
		TeamRow &row = teams_[t];
		row.color = new QToolButton(tab);
		row.color->setToolTip(T("HyperDrive.Dock.TeamColor"));
		row.color->setAutoRaise(true);
		connect(row.color, &QToolButton::clicked, this, [this, team]() { pickColor(team); });
		row.name = new QLabel(tab);
		row.name->setTextInteractionFlags(Qt::TextSelectableByMouse);
		row.score = new QLabel(QStringLiteral("0"), tab);
		row.score->setStyleSheet(QStringLiteral("QLabel { font-size: 20px; font-weight: bold; }"));
		row.score->setAlignment(Qt::AlignCenter);
		row.score->setMinimumWidth(32);
		grid->addWidget(row.color, t, 0);
		grid->addWidget(row.name, t, 1);
		grid->addWidget(smallButton(QStringLiteral("−"), "HyperDrive.Dock.ScoreDown", tab,
					    [this, team]() { send(hd::scoreboard::scoreDown(scoreboard(), team)); }),
				t, 2);
		grid->addWidget(row.score, t, 3);
		grid->addWidget(smallButton(QStringLiteral("+"), "HyperDrive.Dock.ScoreUp", tab,
					    [this, team]() { send(hd::scoreboard::scoreUp(scoreboard(), team)); }),
				t, 4);
	}
	grid->setColumnStretch(1, 1);
	layout->addLayout(grid);

	matchInfo_ = new QLabel(tab);
	matchInfo_->setWordWrap(true);
	layout->addWidget(matchInfo_);

	QHBoxLayout *actions = new QHBoxLayout();
	actions->addWidget(
		button("HyperDrive.Dock.Swap", tab, [this]() { send(hd::scoreboard::swapTeams(scoreboard())); }));
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
				send(hd::scoreboard::reset(scoreboard(), item.kind));
		});
	}
	reset->setMenu(menu);
	actions->addWidget(reset);
	layout->addLayout(actions);
	layout->addStretch(1);
	return tab;
}

QWidget *HyperDriveDock::buildQueueTab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	nextSet_ = new QLabel(tab);
	nextSet_->setWordWrap(true);
	layout->addWidget(nextSet_);
	layout->addWidget(button("HyperDrive.Dock.LoadNext", tab,
				 [this]() { send(hd::scoreboard::loadNextQueued(scoreboard())); }));
	layout->addWidget(button("HyperDrive.Dock.PullStream", tab,
				 [this]() { send(hd::scoreboard::pullStreamSet(scoreboard())); }));
	layout->addWidget(button("HyperDrive.Dock.OpenSetSelector", tab,
				 [this]() { send(hd::scoreboard::openSetSelector(scoreboard())); }));
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
	strikeInfo_ = new QLabel(tab);
	strikeInfo_->setWordWrap(true);
	layout->addWidget(strikeInfo_);

	QHBoxLayout *history = new QHBoxLayout();
	history->addWidget(
		button("HyperDrive.Dock.Undo", tab, [this]() { send(hd::stageStrike::run(scoreboard(), C::Undo)); }));
	history->addWidget(
		button("HyperDrive.Dock.Redo", tab, [this]() { send(hd::stageStrike::run(scoreboard(), C::Redo)); }));
	history->addWidget(button("HyperDrive.Dock.StrikeReset", tab, [this]() {
		if (confirm(T("HyperDrive.Dock.StrikeReset.Confirm")))
			send(hd::stageStrike::run(scoreboard(), C::Reset));
	}));
	layout->addLayout(history);

	QFormLayout *winners = new QFormLayout();
	QHBoxLayout *rps = new QHBoxLayout();
	QHBoxLayout *game = new QHBoxLayout();
	for (int t = 0; t < 2; t++) {
		int team = t + 1;
		rps_[t] = new QPushButton(tab);
		connect(rps_[t], &QPushButton::clicked, this,
			[this, team]() { send(hd::stageStrike::run(scoreboard(), C::RpsWin, team)); });
		rps->addWidget(rps_[t]);
		gameWin_[t] = new QPushButton(tab);
		connect(gameWin_[t], &QPushButton::clicked, this,
			[this, team]() { send(hd::stageStrike::run(scoreboard(), C::GameWin, team)); });
		game->addWidget(gameWin_[t]);
	}
	winners->addRow(T("HyperDrive.Dock.RpsWinner"), rps);
	winners->addRow(T("HyperDrive.Dock.GameWinner"), game);
	layout->addLayout(winners);
	layout->addStretch(1);
	return tab;
}

QWidget *HyperDriveDock::buildBracketTab()
{
	using M = hd::bracketFocus::Mode;
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	QFormLayout *form = new QFormLayout();
	bracketChannel_ = new QComboBox(tab);
	bracketChannel_->addItem(QStringLiteral("main"));
	connect(bracketChannel_, &QComboBox::currentIndexChanged, this, [this]() { refresh(); });
	form->addRow(T("HyperDrive.Dock.Channel"), bracketChannel_);
	layout->addLayout(form);
	bracketInfo_ = new QLabel(tab);
	bracketInfo_->setWordWrap(true);
	layout->addWidget(bracketInfo_);

	auto focus = [this](M mode) {
		send(hd::bracketFocus::set(mode, bracketChannel_->currentText().toStdString(), scoreboard()));
	};
	QGridLayout *grid = new QGridLayout();
	grid->addWidget(button("HyperDrive.Dock.Bracket.Previous", tab, [focus]() { focus(M::PreviousRound); }), 0, 0);
	grid->addWidget(button("HyperDrive.Dock.Bracket.Next", tab, [focus]() { focus(M::NextRound); }), 0, 1);
	grid->addWidget(button("HyperDrive.Dock.Bracket.All", tab, [focus]() { focus(M::All); }), 1, 0);
	grid->addWidget(button("HyperDrive.Dock.Bracket.Follow", tab, [focus]() { focus(M::Follow); }), 1, 1);
	grid->addWidget(button("HyperDrive.Dock.Bracket.Tour", tab, [focus]() { focus(M::Tour); }), 2, 0, 1, 2);
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
		send(hd::scoreboard::teamColor(scoreboard(), team, color.name().toStdString()));
}

void HyperDriveDock::refreshStatus()
{
	Companion *companion = Companion::instance();
	if (!companion)
		return;
	QString address = q(companion->settings().host) + ":" + QString::number(companion->settings().port);
	const char *color = "#e85d5d";
	const char *key = "HyperDrive.Status.Disconnected";
	switch (companion->client().status()) {
	case hd::ConnectionStatus::Connected:
		color = "#4caf50";
		key = "HyperDrive.Status.Connected";
		break;
	case hd::ConnectionStatus::Connecting:
		color = "#e0a030";
		key = "HyperDrive.Status.Connecting";
		break;
	case hd::ConnectionStatus::Disconnected:
		break;
	}
	status_->setText(
		QStringLiteral("<span style=\"color:%1\">●</span> %2").arg(color, T(key).arg(address.toHtmlEscaped())));
}

void HyperDriveDock::refresh()
{
	refreshStatus();
	Companion *companion = Companion::instance();
	if (!companion)
		return;
	companion->client().withState([this](const json &state) {
		// The scoreboards HyperDrive has
		std::vector<int> numbers = hd::scoreboardNumbers(state);
		if (numbers.empty())
			numbers.push_back(1);
		bool same = static_cast<size_t>(scoreboardPicker_->count()) == numbers.size();
		for (size_t i = 0; same && i < numbers.size(); i++)
			same = scoreboardPicker_->itemData(static_cast<int>(i)).toInt() == numbers[i];
		if (!same) {
			QSignalBlocker block(scoreboardPicker_);
			int current = scoreboard();
			scoreboardPicker_->clear();
			for (int n : numbers)
				scoreboardPicker_->addItem(T("HyperDrive.Dock.ScoreboardN").arg(n), n);
			int index = scoreboardPicker_->findData(current);
			scoreboardPicker_->setCurrentIndex(index >= 0 ? index : 0);
		}

		refreshScoreboard(state);
		refreshBattle(state);
		refreshStrike(state);
		refreshBracket(state);
		refreshDisplay(state);
	});
}

void HyperDriveDock::refreshScoreboard(const json &state)
{
	int sb = scoreboard();
	for (int t = 0; t < 2; t++) {
		auto team = hd::scoreboardTeam(state, sb, t + 1);
		TeamRow &row = teams_[t];
		QString name = team && !team->name.empty() ? q(team->name) : T("HyperDrive.Dock.Team").arg(t + 1);
		if (team && team->losers)
			name += QStringLiteral(" [L]");
		row.name->setText(name);
		row.score->setText(QString::number(team ? team->score : 0));
		std::string color = team ? team->color : "";
		row.color->setIcon(swatch(color));
		row.color->setProperty("hdColor", color.empty() ? QColor() : QColor(q(color)));
		rps_[t]->setText(name);
		gameWin_[t]->setText(name);
	}

	QStringList info;
	std::string match = hd::matchName(state, sb);
	if (!match.empty())
		info << q(match);
	int bestOf = hd::bestOf(state, sb);
	if (bestOf > 0)
		info << T("HyperDrive.Dock.BestOf").arg(bestOf);
	matchInfo_->setText(info.join(QStringLiteral(" · ")));

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

void HyperDriveDock::refreshStrike(const json &state)
{
	hd::StrikeInfo strike = hd::stageStrikeInfo(state, scoreboard());
	QString text = T("HyperDrive.Dock.GameN").arg(strike.game + 1);
	if (strike.stageSelected)
		text += QStringLiteral(" · ") + T("HyperDrive.Dock.Stage").arg(q(strike.stage));
	else if (strike.active)
		text += QStringLiteral(" · ") + T("HyperDrive.Dock.Striking");
	strikeInfo_->setText(text);
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
