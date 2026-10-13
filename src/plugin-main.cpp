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
#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QMainWindow>

#include <ixwebsocket/IXNetSystem.h>

#include "companion.hpp"
#include "hotkeys.hpp"
#include "layout-source.hpp"
#include "plugin-support.h"
#include "ui/dock.hpp"
#include "ui/settings-dialog.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("HyperDrive.Description");
}

namespace {

constexpr const char *kDockId = "hyperdrive-companion-dock";
constexpr const char *kSaveKey = "hyperdrive-companion";

// The plugin's settings in the scene collection: the config, and the hotkeys' keys
void onSave(obs_data_t *saveData, bool saving, void *)
{
	Companion *companion = Companion::instance();
	if (!companion)
		return;
	if (saving) {
		nlohmann::json config;
		companion->saveCollection(config);
		obs_data_t *data = obs_data_create();
		obs_data_t *configData = obs_data_create_from_json(config.dump().c_str());
		obs_data_set_obj(data, "config", configData);
		obs_data_release(configData);
		obs_data_t *keys = hotkeys_save();
		obs_data_set_obj(data, "hotkeys", keys);
		obs_data_release(keys);
		obs_data_set_obj(saveData, kSaveKey, data);
		obs_data_release(data);
		return;
	}

	obs_data_t *data = obs_data_get_obj(saveData, kSaveKey);
	obs_data_t *configData = data ? obs_data_get_obj(data, "config") : nullptr;
	const char *json = configData ? obs_data_get_json(configData) : nullptr;
	companion->loadCollection(json ? nlohmann::json::parse(json, nullptr, false) : nlohmann::json());
	obs_data_t *keys = data ? obs_data_get_obj(data, "hotkeys") : nullptr;
	hotkeys_load(keys);
	obs_data_release(keys);
	obs_data_release(configData);
	obs_data_release(data);
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	Companion *companion = Companion::instance();
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		if (companion)
			companion->onSceneChanged();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		if (companion)
			companion->onCollectionChanging();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		// Before Qt goes away
		hotkeys_unregister();
		Companion::destroy();
		break;
	default:
		break;
	}
}

} // namespace

bool obs_module_load(void)
{
	ix::initNetSystem();
	layout_source_register();
	Companion::create();
	hotkeys_register();

	obs_frontend_add_save_callback(onSave, nullptr);
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	obs_frontend_add_tools_menu_item(
		obs_module_text("HyperDrive.Settings.Title"), [](void *) { SettingsDialog::showDialog(); }, nullptr);

	QMainWindow *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_add_dock_by_id(kDockId, obs_module_text("HyperDrive.Name"), new HyperDriveDock(main));

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	obs_frontend_remove_save_callback(onSave, nullptr);
	hotkeys_unregister();
	Companion::destroy();
	ix::uninitNetSystem();
	obs_log(LOG_INFO, "plugin unloaded");
}
