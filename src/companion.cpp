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
#include "companion.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <QApplication>
#include <QMetaObject>
#include <QPointer>
#include <QTime>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#include "bracket/paint.hpp"
#include "plugin-support.h"
#include "text.hpp"

namespace {

Companion *companion = nullptr;

const char *kSettingsFile = "settings.json";

QString q(const std::string &s)
{
	return QString::fromStdString(s);
}

QString describe(const hd::Request &request, const hd::CommandResult &result)
{
	if (result.status == 0)
		return T("HyperDrive.Error.Unreachable");
	QString body = QString::fromStdString(result.body).trimmed();
	if (body.size() > 120)
		body = body.left(120) + QStringLiteral("…");
	return QStringLiteral("%1 %2: %3")
		.arg(QString::fromStdString(request.path))
		.arg(result.status)
		.arg(body.isEmpty() ? T("HyperDrive.Error.Refused") : body);
}

QString eventText(const hd::TriggerEvent &event)
{
	QString text = kindText(event.kind);
	if (event.scoreboard > 0)
		text += QStringLiteral(" · SB%1").arg(event.scoreboard);
	if (!event.param.empty() && event.kind != hd::TriggerKind::Timer)
		text += QStringLiteral(" · ") + q(event.param);
	return text;
}

} // namespace

Companion *Companion::instance()
{
	return companion;
}

void Companion::create()
{
	if (!companion)
		companion = new Companion();
}

void Companion::destroy()
{
	delete companion;
	companion = nullptr;
}

Companion::Companion()
{
	loadSettings();
	for (const hd::InstanceConfig &config : settings_.instances)
		instances_.push_back(connectInstance(config));

	minuteTimer_.setInterval(60 * 1000);
	connect(&minuteTimer_, &QTimer::timeout, this,
		[this]() { dispatch({hd::TriggerKind::Timer, 0, std::to_string(++minutes_)}, ""); });
	minuteTimer_.start();
}

Companion::~Companion()
{
	minuteTimer_.stop();
	for (const auto &drive : instances_) {
		drive->client->setStateCallback(nullptr);
		drive->client->setStatusCallback(nullptr);
		drive->client->disconnect();
	}
}

std::shared_ptr<HyperDrive> Companion::connectInstance(const hd::InstanceConfig &config)
{
	auto drive = std::make_shared<HyperDrive>();
	drive->config = config;
	std::weak_ptr<HyperDrive> weak = drive;
	// The client calls back on its threads: hand it to the UI thread,
	// once for any number of changes in between
	drive->client->setStateCallback([this, weak]() {
		stateVersion_++;
		auto d = weak.lock();
		if (d && !d->stateQueued.exchange(true))
			QMetaObject::invokeMethod(
				this,
				[this, weak]() {
					if (auto d = weak.lock())
						handleState(*d);
				},
				Qt::QueuedConnection);
	});
	drive->client->setStatusCallback([this, weak](hd::ConnectionStatus status) {
		QMetaObject::invokeMethod(
			this,
			[this, weak, status]() {
				if (auto d = weak.lock())
					handleStatus(*d, status);
			},
			Qt::QueuedConnection);
	});
	drive->client->connect(hd::parseAddress(config.host, config.port));
	return drive;
}

std::shared_ptr<HyperDrive> Companion::hyperdrive(const std::string &id) const
{
	std::lock_guard<std::mutex> lock(instancesMutex_);
	for (const auto &drive : instances_) {
		if (drive->config.id == id)
			return drive;
	}
	return instances_.front();
}

std::vector<std::shared_ptr<HyperDrive>> Companion::hyperdrives() const
{
	std::lock_guard<std::mutex> lock(instancesMutex_);
	return instances_;
}

std::string Companion::baseUrl(const std::string &id) const
{
	return hd::baseUrl(hyperdrive(id)->client->address());
}

void Companion::loadSettings()
{
	char *path = obs_module_config_path(kSettingsFile);
	obs_data_t *data = path ? obs_data_create_from_json_file_safe(path, "bak") : nullptr;
	bfree(path);
	const char *json = data ? obs_data_get_json(data) : nullptr;
	settings_ = hd::globalConfigFromJson(json ? nlohmann::json::parse(json, nullptr, false) : nlohmann::json());
	obs_data_release(data);
}

void Companion::saveSettings() const
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *path = obs_module_config_path(kSettingsFile);
	if (!path)
		return;
	obs_data_t *data = obs_data_create_from_json(hd::toJson(settings_).dump().c_str());
	if (!data || !obs_data_save_json_safe(data, path, "tmp", "bak"))
		obs_log(LOG_WARNING, "Couldn't save the settings to %s", path);
	obs_data_release(data);
	bfree(path);
}

void Companion::setSettings(const hd::GlobalConfig &settings)
{
	bool dirChanged = settings.hyperdriveDir != settings_.hyperdriveDir;
	settings_ = hd::globalConfigFromJson(hd::toJson(settings));
	bool addressChanged = false;
	std::vector<std::shared_ptr<HyperDrive>> next, removed;
	{
		std::lock_guard<std::mutex> lock(instancesMutex_);
		for (hd::InstanceConfig &config : settings_.instances) {
			hd::Address address = hd::parseAddress(config.host, config.port);
			config.host = address.host;
			config.port = address.port;
			auto it = std::find_if(instances_.begin(), instances_.end(),
					       [&](const auto &d) { return d->config.id == config.id; });
			if (it == instances_.end()) {
				next.push_back(connectInstance(config));
				continue;
			}
			if ((*it)->config.host != config.host || (*it)->config.port != config.port) {
				(*it)->client->connect(address);
				addressChanged = true;
			}
			(*it)->config = config;
			next.push_back(*it);
		}
		for (const auto &drive : instances_) {
			if (std::find(next.begin(), next.end(), drive) == next.end())
				removed.push_back(drive);
		}
		instances_ = next;
	}
	for (const auto &drive : removed) {
		drive->client->setStateCallback(nullptr);
		drive->client->setStatusCallback(nullptr);
		drive->client->disconnect();
	}
	saveSettings();

	if (addressChanged || !removed.empty()) {
		updateLayoutSources();
		if (hd::bracket::Assets *assets = hd::bracket::Assets::instance())
			assets->clear();
	}
	if (addressChanged || dirChanged) {
		// Listed again when asked for, or once connected
		for (const auto &drive : next) {
			std::lock_guard<std::mutex> lock(drive->layoutsMutex);
			drive->layoutsLoaded = false;
		}
	}
	emit settingsChanged();
}

std::string Companion::dockInstance() const
{
	return hyperdrive(settings_.dockInstance)->config.id;
}

void Companion::setDockInstance(const std::string &id)
{
	settings_.dockInstance = id;
	saveSettings();
	emit settingsChanged();
}

void Companion::setConfig(const hd::CollectionConfig &config)
{
	config_ = config;
	obs_frontend_save();
	emit configChanged();
}

void Companion::saveCollection(nlohmann::json &out) const
{
	out = hd::toJson(config_);
}

void Companion::loadCollection(const nlohmann::json &in)
{
	epoch_++;
	config_ = hd::collectionConfigFromJson(in);
	emit configChanged();
}

void Companion::command(const hd::Request &request, const std::string &instance)
{
	auto drive = hyperdrive(instance);
	QPointer<Companion> self(this);
	QString name = q(drive->config.name);
	drive->client->command(request, [self, request, name](const hd::CommandResult &result) {
		if (result.ok)
			return;
		obs_log(LOG_WARNING, "%s failed (%d): %s", request.path.c_str(), result.status, result.body.c_str());
		QString message = name + QStringLiteral(" · ") + describe(request, result);
		QMetaObject::invokeMethod(
			qApp,
			[self, message]() {
				if (self && self->settings_.showAlerts)
					emit self->commandFailed(message);
			},
			Qt::QueuedConnection);
	});
}

void Companion::hotkeyFromAnyThread(const hd::NamedCommand &command)
{
	auto make = command.make;
	if (companion)
		QMetaObject::invokeMethod(
			companion,
			[make]() {
				if (!companion)
					return;
				const hd::CollectionConfig &config = companion->config_;
				companion->command(make(config.hotkeyScoreboard), config.hotkeyInstance.empty()
											  ? companion->dockInstance()
											  : config.hotkeyInstance);
			},
			Qt::QueuedConnection);
}

void Companion::automationHotkeyFromAnyThread(int number)
{
	if (companion)
		QMetaObject::invokeMethod(
			companion,
			[number]() {
				if (companion)
					companion->dispatch({hd::TriggerKind::Hotkey, 0, std::to_string(number)}, "");
			},
			Qt::QueuedConnection);
}

std::vector<hd::LayoutFolder> Companion::layouts(const std::string &instance, bool refresh)
{
	auto drive = hyperdrive(instance);
	{
		std::lock_guard<std::mutex> lock(drive->layoutsMutex);
		if (drive->layoutsLoaded && !refresh)
			return drive->layouts;
	}
	std::vector<hd::LayoutFolder> found;
	bool fromServer = false;
	if (drive->client->status() == hd::ConnectionStatus::Connected || refresh) {
		hd::CommandResult result = drive->client->commandSync(hd::layouts(), 2);
		if (result.ok) {
			found = hd::parseLayouts(nlohmann::json::parse(result.body, nullptr, false));
			fromServer = !found.empty();
		} else {
			obs_log(LOG_WARNING, "Couldn't get the layouts from %s (%d): %s", drive->config.name.c_str(),
				result.status, result.body.substr(0, 200).c_str());
		}
	}
	if (found.empty())
		found = hd::scanLayouts(settings_.hyperdriveDir);

	std::lock_guard<std::mutex> lock(drive->layoutsMutex);
	drive->layouts = found;
	drive->layoutsLoaded = true;
	drive->layoutsFromServer = fromServer;
	return found;
}

bool Companion::layoutsFromServer(const std::string &instance) const
{
	auto drive = hyperdrive(instance);
	std::lock_guard<std::mutex> lock(drive->layoutsMutex);
	return drive->layoutsFromServer;
}

void Companion::handleState(HyperDrive &drive)
{
	drive.stateQueued = false;
	nlohmann::json snapshot;
	drive.client->withState([&snapshot](const nlohmann::json &state) { snapshot = hd::triggerSnapshot(state); });
	nlohmann::json before = std::move(drive.snapshot);
	drive.snapshot = std::move(snapshot);
	// The first state after connecting only says which game is picked
	for (const hd::TriggerEvent &event : hd::detectTriggers(before, drive.snapshot))
		dispatch(event, drive.config.id);
	emit stateChanged(q(drive.config.id));
}

void Companion::handleStatus(HyperDrive &drive, hd::ConnectionStatus status)
{
	std::string id = drive.config.id;
	if (status == hd::ConnectionStatus::Connected) {
		layouts(id, true);
		if (hd::bracket::Assets *assets = hd::bracket::Assets::instance())
			assets->retry();
		if (!drive.wasConnected)
			dispatch({hd::TriggerKind::Connected}, id);
		drive.wasConnected = true;
	} else {
		drive.snapshot = nullptr;
		if (status == hd::ConnectionStatus::Disconnected && drive.wasConnected) {
			drive.wasConnected = false;
			dispatch({hd::TriggerKind::Disconnected}, id);
		}
	}
	emit statusChanged(q(id));
}

// ---------------------------------------------------------------------
// Automation

void Companion::dispatch(const hd::TriggerEvent &event, const std::string &instance)
{
	if (!config_.automationEnabled)
		return;
	hd::ConditionContext context;
	context.scene = currentSceneName();
	context.scoreboard = event.scoreboard;
	context.recording = obs_frontend_recording_active();
	context.streaming = obs_frontend_streaming_active();
	context.replayBuffer = obs_frontend_replay_buffer_active();
	for (const auto &drive : hyperdrives()) {
		if (drive->client->status() == hd::ConnectionStatus::Connected)
			context.connected.push_back(drive->config.id);
	}
	// A copy: the actions run later, the config may change in between
	std::vector<hd::Rule> rules = config_.rules;
	for (const hd::Rule &rule : rules) {
		if (!hd::ruleArmed(config_, rule) || !hd::ruleMatches(rule, event, instance))
			continue;
		// The event's instance, or the rule's for OBS's events
		std::string on = instance.empty() ? rule.instance : instance;
		bool holds = false;
		hyperdrive(on)->client->withState([&](const nlohmann::json &state) {
			context.state = &state;
			holds = hd::conditionsHold(rule, context);
		});
		context.state = nullptr;
		if (holds)
			run(rule, on, event.scoreboard, eventText(event));
	}
}

void Companion::testRule(const hd::Rule &rule)
{
	run(rule, rule.instance, rule.scoreboard, T("HyperDrive.Automation.TestRun"));
}

void Companion::run(const hd::Rule &rule, const std::string &instance, int scoreboard, const QString &why)
{
	auto drive = hyperdrive(instance);
	log(QStringLiteral("%1 · %2 → %3")
		    .arg(q(drive->config.name), why, rule.name.empty() ? kindText(rule.trigger) : q(rule.name)));
	runActions(std::make_shared<std::vector<hd::Action>>(rule.actions), 0, drive->config.id, scoreboard, epoch_);
}

void Companion::runActions(std::shared_ptr<std::vector<hd::Action>> actions, size_t index, const std::string &instance,
			   int scoreboard, uint64_t epoch)
{
	for (; index < actions->size() && epoch == epoch_; index++) {
		const hd::Action &action = (*actions)[index];
		if (action.kind == hd::ActionKind::Wait) {
			int ms = std::max(0, std::atoi(action.value.c_str()));
			QTimer::singleShot(ms, this, [this, actions, index, instance, scoreboard, epoch]() {
				runActions(actions, index + 1, instance, scoreboard, epoch);
			});
			return;
		}
		runAction(action, instance, scoreboard);
	}
}

void Companion::runAction(const hd::Action &a, const std::string &instance, int scoreboard)
{
	using K = hd::ActionKind;
	switch (a.kind) {
	case K::SwitchScene:
		switchScene(a.target);
		break;
	case K::ShowLayouts:
	case K::HideLayouts:
		for (const hd::DisplayTarget &target : hd::DisplayTarget::parseList(a.target))
			command(hd::display::set(target, a.kind == K::ShowLayouts ? hd::display::Action::Show
										  : hd::display::Action::Hide),
				instance);
		break;
	case K::ShowSource:
	case K::HideSource: {
		obs_source_t *sceneSource = a.value.empty() ? obs_frontend_get_current_scene()
							    : obs_get_source_by_name(a.value.c_str());
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		obs_sceneitem_t *item = scene ? obs_scene_find_source_recursive(scene, a.target.c_str()) : nullptr;
		if (item)
			obs_sceneitem_set_visible(item, a.kind == K::ShowSource);
		else
			obs_log(LOG_WARNING, "Source \"%s\" not found in the scene", a.target.c_str());
		obs_source_release(sceneSource);
		break;
	}
	case K::ToggleFilter: {
		obs_source_t *source = obs_get_source_by_name(a.target.c_str());
		obs_source_t *filter = source ? obs_source_get_filter_by_name(source, a.value.c_str()) : nullptr;
		if (filter)
			obs_source_set_enabled(filter, !obs_source_enabled(filter));
		else
			obs_log(LOG_WARNING, "Filter \"%s\" of \"%s\" not found", a.value.c_str(), a.target.c_str());
		obs_source_release(filter);
		obs_source_release(source);
		break;
	}
	case K::Command: {
		int sb = std::atoi(a.value.c_str());
		if (auto request = hd::namedCommand(a.target, sb > 0 ? sb : std::max(1, scoreboard)))
			command(*request, instance);
		break;
	}
	case K::StartRecording:
		if (!obs_frontend_recording_active())
			obs_frontend_recording_start();
		break;
	case K::StopRecording:
		if (obs_frontend_recording_active())
			obs_frontend_recording_stop();
		break;
	case K::SaveReplay:
		obs_frontend_replay_buffer_save();
		break;
	case K::Wait:
	case K::Count:
		break;
	}
}

void Companion::log(const QString &line)
{
	QString entry = QTime::currentTime().toString(QStringLiteral("HH:mm:ss  ")) + line;
	obs_log(LOG_INFO, "%s", line.toUtf8().constData());
	log_.append(entry);
	while (log_.size() > 100)
		log_.removeFirst();
	emit automationRan(entry);
}

void Companion::switchScene(const std::string &scene)
{
	if (scene.empty())
		return;
	obs_source_t *source = obs_get_source_by_name(scene.c_str());
	if (!source) {
		obs_log(LOG_WARNING, "Scene \"%s\" not found", scene.c_str());
		return;
	}
	if (obs_source_is_scene(source) && scene != currentSceneName()) {
		if (obs_frontend_preview_program_mode_active()) {
			// Studio Mode: take it live, through the preview
			obs_frontend_set_current_preview_scene(source);
			obs_frontend_preview_program_trigger_transition();
		} else {
			obs_frontend_set_current_scene(source);
		}
	}
	obs_source_release(source);
}

void Companion::onSceneChanged()
{
	dispatch({hd::TriggerKind::SceneLive, 0, currentSceneName()}, "");
}

void Companion::onObsEvent(hd::TriggerKind kind)
{
	dispatch({kind}, "");
}

void Companion::onCollectionChanging()
{
	epoch_++;
}

std::string Companion::currentSceneName()
{
	obs_source_t *scene = obs_frontend_get_current_scene();
	std::string name = scene ? obs_source_get_name(scene) : "";
	obs_source_release(scene);
	return name;
}

std::vector<std::string> Companion::sceneNames()
{
	std::vector<std::string> names;
	char **list = obs_frontend_get_scene_names();
	for (char **name = list; name && *name; name++)
		names.emplace_back(*name);
	bfree(list);
	return names;
}

void Companion::updateLayoutSources()
{
	// Their URLs have HyperDrive's address in them
	obs_enum_sources(
		[](void *, obs_source_t *source) {
			if (strcmp(obs_source_get_unversioned_id(source), "hyperdrive_layout") == 0)
				obs_source_update(source, nullptr);
			return true;
		},
		nullptr);
}

obs_property *addInstanceList(obs_properties *props)
{
	obs_property_t *list = obs_properties_add_list(props, "instance", obs_module_text("HyperDrive.Instance"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(list, obs_module_text("HyperDrive.Instance.Default"), "");
	if (Companion *companion = Companion::instance()) {
		for (const auto &drive : companion->hyperdrives())
			obs_property_list_add_string(list, drive->config.name.c_str(), drive->config.id.c_str());
	}
	return list;
}
