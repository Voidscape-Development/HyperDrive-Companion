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
 * The plugin's hub, on OBS's UI thread: the connections to the HyperDrive
 * instances, the settings (global, and per scene collection), the layout
 * lists, and the automation. The dock, the settings dialog, the hotkeys and
 * the sources all go through it.
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <nlohmann/json.hpp>

#include "core/config.hpp"
#include "core/layouts.hpp"
#include "core/triggers.hpp"
#include "net/hyperdrive-client.hpp"

struct obs_properties;
struct obs_property;

/** One HyperDrive the plugin connects to */
struct HyperDrive {
	hd::InstanceConfig config;
	std::unique_ptr<hd::HyperDriveClient> client = std::make_unique<hd::HyperDriveClient>();
	std::atomic<bool> stateQueued{false};
	/** The state's last triggerSnapshot, for the automation (UI thread) */
	nlohmann::json snapshot;
	bool wasConnected = false;

	std::mutex layoutsMutex;
	std::vector<hd::LayoutFolder> layouts;
	bool layoutsLoaded = false;
	bool layoutsFromServer = false;
};

class Companion : public QObject {
	Q_OBJECT

public:
	static Companion *instance();
	/** Creates the instance, on module load */
	static void create();
	/** On OBS exit */
	static void destroy();

	/** The instance with this id, or the default one (never null). From any thread */
	std::shared_ptr<HyperDrive> hyperdrive(const std::string &id = {}) const;
	std::vector<std::shared_ptr<HyperDrive>> hyperdrives() const;
	/** Its web server, "http://127.0.0.1:5500". From any thread */
	std::string baseUrl(const std::string &id = {}) const;
	/** Changes with any instance's state, from any thread (for the bracket focus sources) */
	uint64_t stateVersion() const { return stateVersion_.load(); }

	const hd::GlobalConfig &settings() const { return settings_; }
	void setSettings(const hd::GlobalConfig &settings);
	/** The instance the dock controls */
	std::string dockInstance() const;
	void setDockInstance(const std::string &id);

	/** The scene collection's settings */
	const hd::CollectionConfig &config() const { return config_; }
	void setConfig(const hd::CollectionConfig &config);

	/** Runs a command on an instance; a refused or failed one shows in the dock */
	void command(const hd::Request &request, const std::string &instance = {});
	/** A hotkey, from any thread: on the hotkeys' instance and scoreboard */
	static void hotkeyFromAnyThread(const hd::NamedCommand &command);
	/** An automation hotkey (1-10), from any thread */
	static void automationHotkeyFromAnyThread(int number);

	/** An instance's layouts, from HyperDrive or its folder (cached; refresh fetches them again) */
	std::vector<hd::LayoutFolder> layouts(const std::string &instance = {}, bool refresh = false);
	bool layoutsFromServer(const std::string &instance = {}) const;

	/** Runs a rule's actions now, whatever its trigger and conditions */
	void testRule(const hd::Rule &rule);
	/** What the automation did lately */
	const QStringList &automationLog() const { return log_; }

	/** Saves to/loads from the scene collection */
	void saveCollection(nlohmann::json &out) const;
	void loadCollection(const nlohmann::json &in);

	/** OBS frontend events */
	void onSceneChanged();
	void onCollectionChanging();
	void onObsEvent(hd::TriggerKind kind);

	static std::string currentSceneName();
	static std::vector<std::string> sceneNames();

signals:
	/** An instance's state changed (coalesced) */
	void stateChanged(const QString &instance);
	void statusChanged(const QString &instance);
	void commandFailed(const QString &message);
	void configChanged();
	void settingsChanged();
	void automationRan(const QString &line);

private:
	Companion();
	~Companion() override;

	void loadSettings();
	void saveSettings() const;
	std::shared_ptr<HyperDrive> connectInstance(const hd::InstanceConfig &config);
	void handleState(HyperDrive &hd);
	void handleStatus(HyperDrive &hd, hd::ConnectionStatus status);

	void dispatch(const hd::TriggerEvent &event, const std::string &instance);
	void run(const hd::Rule &rule, const std::string &instance, int scoreboard, const QString &why);
	void runActions(std::shared_ptr<std::vector<hd::Action>> actions, size_t index, const std::string &instance,
			int scoreboard, uint64_t epoch);
	void runAction(const hd::Action &action, const std::string &instance, int scoreboard);
	void log(const QString &line);
	void switchScene(const std::string &scene);
	void updateLayoutSources();

	mutable std::mutex instancesMutex_;
	std::vector<std::shared_ptr<HyperDrive>> instances_;
	hd::GlobalConfig settings_;
	hd::CollectionConfig config_;

	std::atomic<uint64_t> stateVersion_{0};
	/** Pending actions of an older scene collection don't run */
	uint64_t epoch_ = 0;
	QTimer minuteTimer_;
	int minutes_ = 0;
	QStringList log_;
};

/** Adds a source's "instance" list: the default instance ("") or one of them */
obs_property *addInstanceList(obs_properties *props);
