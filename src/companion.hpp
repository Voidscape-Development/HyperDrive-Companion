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
 * The plugin's hub, on OBS's UI thread: the connection to HyperDrive, the
 * settings (global, and per scene collection), the layout list, and the
 * scene automation. The dock, the settings dialog, the hotkeys and the
 * layout sources all go through it.
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <QTimer>

#include <nlohmann/json.hpp>

#include "core/config.hpp"
#include "core/layouts.hpp"
#include "net/hyperdrive-client.hpp"

struct GlobalSettings {
	std::string host = "localhost";
	int port = 5500;
	/** HyperDrive's folder, to list the layouts when it has no GET /layouts */
	std::string hyperdriveDir;
	bool showAlerts = true;
};

class Companion : public QObject {
	Q_OBJECT

public:
	static Companion *instance();
	/** Creates the instance, on module load */
	static void create();
	/** On OBS exit */
	static void destroy();

	hd::HyperDriveClient &client() { return *client_; }

	const GlobalSettings &settings() const { return settings_; }
	void setSettings(const GlobalSettings &settings);
	std::string baseUrl() const;

	/** The scene collection's settings */
	const hd::CollectionConfig &config() const { return config_; }
	void setConfig(const hd::CollectionConfig &config);
	/** For the hotkeys, on their thread */
	int hotkeyScoreboard() const { return hotkeyScoreboard_.load(); }

	/** Runs a command; a refused or failed one shows in the dock */
	void command(const hd::Request &request);
	/** From any thread */
	static void commandFromAnyThread(const hd::Request &request);

	/** The layouts, from HyperDrive or its folder (cached; refresh fetches them again) */
	std::vector<hd::LayoutFolder> layouts(bool refresh = false);
	/** Where the cached layout list came from */
	bool layoutsFromServer() const;

	/** Saves to/loads from the scene collection */
	void saveCollection(nlohmann::json &out) const;
	void loadCollection(const nlohmann::json &in);

	/** OBS frontend events */
	void onSceneChanged();
	void onCollectionChanging();

	static std::string currentSceneName();
	static std::vector<std::string> sceneNames();

signals:
	/** The state changed (coalesced) */
	void stateChanged();
	void statusChanged(int status);
	void commandFailed(const QString &message);
	void configChanged();
	void settingsChanged();

private:
	Companion();
	~Companion() override;

	void loadSettings();
	void saveSettings() const;
	void handleState();
	void runAutomation(const nlohmann::json &scores);
	void switchScene(const std::string &scene);
	void updateLayoutSources();

	std::unique_ptr<hd::HyperDriveClient> client_;
	GlobalSettings settings_;
	hd::CollectionConfig config_;
	std::atomic<int> hotkeyScoreboard_{1};

	std::atomic<bool> stateQueued_{false};
	/** "score" of the last state seen, for the automation */
	nlohmann::json lastScores_;
	QTimer sceneTimer_;
	std::string pendingScene_;

	mutable std::mutex layoutsMutex_;
	std::vector<hd::LayoutFolder> layouts_;
	bool layoutsLoaded_ = false;
	bool layoutsFromServer_ = false;
};
