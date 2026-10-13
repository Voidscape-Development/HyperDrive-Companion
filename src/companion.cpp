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

#include <cstring>

#include <QApplication>
#include <QMetaObject>
#include <QPointer>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#include "core/triggers.hpp"
#include "plugin-support.h"
#include "text.hpp"

namespace {

Companion *companion = nullptr;

const char *kSettingsFile = "settings.json";

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

Companion::Companion() : client_(std::make_unique<hd::HyperDriveClient>())
{
	sceneTimer_.setSingleShot(true);
	connect(&sceneTimer_, &QTimer::timeout, this, [this]() {
		std::string scene = pendingScene_;
		pendingScene_.clear();
		switchScene(scene);
	});

	// The client calls back on its threads: hand it to the UI thread,
	// once for any number of changes in between
	client_->setStateCallback([this]() {
		if (!stateQueued_.exchange(true))
			QMetaObject::invokeMethod(this, [this]() { handleState(); }, Qt::QueuedConnection);
	});
	client_->setStatusCallback([this](hd::ConnectionStatus status) {
		QMetaObject::invokeMethod(
			this,
			[this, status]() {
				if (status != hd::ConnectionStatus::Connected)
					lastScores_ = nullptr;
				else
					layouts(true);
				emit statusChanged(static_cast<int>(status));
			},
			Qt::QueuedConnection);
	});

	loadSettings();
	client_->connect(hd::parseAddress(settings_.host, settings_.port));
}

Companion::~Companion()
{
	sceneTimer_.stop();
	client_->setStateCallback(nullptr);
	client_->setStatusCallback(nullptr);
	client_->disconnect();
}

void Companion::loadSettings()
{
	char *path = obs_module_config_path(kSettingsFile);
	obs_data_t *data = path ? obs_data_create_from_json_file_safe(path, "bak") : nullptr;
	bfree(path);
	if (!data)
		return;
	obs_data_set_default_string(data, "host", "localhost");
	obs_data_set_default_int(data, "port", 5500);
	obs_data_set_default_bool(data, "show_alerts", true);
	settings_.host = obs_data_get_string(data, "host");
	settings_.port = static_cast<int>(obs_data_get_int(data, "port"));
	settings_.hyperdriveDir = obs_data_get_string(data, "hyperdrive_dir");
	settings_.showAlerts = obs_data_get_bool(data, "show_alerts");
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
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "host", settings_.host.c_str());
	obs_data_set_int(data, "port", settings_.port);
	obs_data_set_string(data, "hyperdrive_dir", settings_.hyperdriveDir.c_str());
	obs_data_set_bool(data, "show_alerts", settings_.showAlerts);
	if (!obs_data_save_json_safe(data, path, "tmp", "bak"))
		obs_log(LOG_WARNING, "Couldn't save the settings to %s", path);
	obs_data_release(data);
	bfree(path);
}

void Companion::setSettings(const GlobalSettings &settings)
{
	bool addressChanged = settings.host != settings_.host || settings.port != settings_.port;
	bool dirChanged = settings.hyperdriveDir != settings_.hyperdriveDir;
	settings_ = settings;
	hd::Address address = hd::parseAddress(settings_.host, settings_.port);
	settings_.host = address.host;
	settings_.port = address.port;
	saveSettings();

	if (addressChanged) {
		client_->connect(address);
		updateLayoutSources();
	}
	if (addressChanged || dirChanged)
		layouts(true);
	emit settingsChanged();
}

std::string Companion::baseUrl() const
{
	return hd::baseUrl(hd::parseAddress(settings_.host, settings_.port));
}

void Companion::setConfig(const hd::CollectionConfig &config)
{
	config_ = config;
	hotkeyScoreboard_ = config_.hotkeyScoreboard;
	obs_frontend_save();
	emit configChanged();
}

void Companion::saveCollection(nlohmann::json &out) const
{
	out = hd::toJson(config_);
}

void Companion::loadCollection(const nlohmann::json &in)
{
	sceneTimer_.stop();
	pendingScene_.clear();
	config_ = hd::collectionConfigFromJson(in);
	hotkeyScoreboard_ = config_.hotkeyScoreboard;
	emit configChanged();
}

void Companion::command(const hd::Request &request)
{
	QPointer<Companion> self(this);
	client_->command(request, [self, request](const hd::CommandResult &result) {
		if (result.ok)
			return;
		obs_log(LOG_WARNING, "%s failed (%d): %s", request.path.c_str(), result.status, result.body.c_str());
		QString message = describe(request, result);
		QMetaObject::invokeMethod(
			qApp,
			[self, message]() {
				if (self && self->settings_.showAlerts)
					emit self->commandFailed(message);
			},
			Qt::QueuedConnection);
	});
}

void Companion::commandFromAnyThread(const hd::Request &request)
{
	if (companion)
		companion->command(request);
}

std::vector<hd::LayoutFolder> Companion::layouts(bool refresh)
{
	{
		std::lock_guard<std::mutex> lock(layoutsMutex_);
		if (layoutsLoaded_ && !refresh)
			return layouts_;
	}
	std::vector<hd::LayoutFolder> found;
	bool fromServer = false;
	if (client_->status() == hd::ConnectionStatus::Connected || refresh) {
		hd::CommandResult result = client_->commandSync(hd::layouts(), 2);
		if (result.ok) {
			found = hd::parseLayouts(nlohmann::json::parse(result.body, nullptr, false));
			fromServer = !found.empty();
		}
	}
	if (found.empty())
		found = hd::scanLayouts(settings_.hyperdriveDir);

	std::lock_guard<std::mutex> lock(layoutsMutex_);
	layouts_ = found;
	layoutsLoaded_ = true;
	layoutsFromServer_ = fromServer;
	return layouts_;
}

bool Companion::layoutsFromServer() const
{
	std::lock_guard<std::mutex> lock(layoutsMutex_);
	return layoutsFromServer_;
}

void Companion::handleState()
{
	stateQueued_ = false;
	runAutomation(client_->statePart("score"));
	emit stateChanged();
}

void Companion::runAutomation(const nlohmann::json &scores)
{
	nlohmann::json after = nlohmann::json::object();
	if (!scores.is_null())
		after["score"] = scores;
	nlohmann::json before = nlohmann::json::object();
	if (!lastScores_.is_null())
		before["score"] = lastScores_;
	lastScores_ = scores;

	if (!config_.automationEnabled || config_.rules.empty())
		return;
	std::vector<hd::TriggerEvent> events = hd::detectTriggers(before, after);
	if (events.empty())
		return;
	int index = hd::matchRule(config_.rules, events, currentSceneName());
	if (index < 0)
		return;
	const hd::AutomationRule &rule = config_.rules[static_cast<size_t>(index)];
	obs_log(LOG_INFO, "%s: switching to \"%s\" in %d ms", hd::triggerId(rule.trigger), rule.scene.c_str(),
		rule.delayMs);
	// A later event replaces a switch still waiting
	pendingScene_ = rule.scene;
	sceneTimer_.start(rule.delayMs);
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
	if (obs_source_is_scene(source)) {
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
	std::string scene = currentSceneName();
	for (const hd::SceneDisplayRule &rule : hd::sceneDisplayRules(config_.sceneDisplay, scene)) {
		if (auto target = hd::DisplayTarget::parse(rule.target))
			command(hd::display::set(*target, rule.action));
	}
}

void Companion::onCollectionChanging()
{
	sceneTimer_.stop();
	pendingScene_.clear();
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
