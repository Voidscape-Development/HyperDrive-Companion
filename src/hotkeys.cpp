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
#include "hotkeys.hpp"

#include <string>
#include <vector>

#include <obs-module.h>

#include "companion.hpp"
#include "core/commands.hpp"
#include "text.hpp"

namespace {

constexpr int kAutomationHotkeys = 10;

struct Hotkey {
	std::string name;
	std::string description;
	/** A command, or else an automation hotkey's number */
	const hd::NamedCommand *command = nullptr;
	int automation = 0;
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

std::vector<Hotkey> &hotkeys()
{
	static std::vector<Hotkey> all = []() {
		std::vector<Hotkey> list;
		for (const hd::NamedCommand &command : hd::namedCommands())
			list.push_back(
				{std::string("hyperdrive.") + command.id, obs_module_text(command.textKey), &command});
		for (int n = 1; n <= kAutomationHotkeys; n++)
			list.push_back({"hyperdrive.automation." + std::to_string(n),
					T("HyperDrive.Hotkey.Automation").arg(n).toStdString(), nullptr, n});
		return list;
	}();
	return all;
}

void pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool down)
{
	if (!down)
		return;
	const Hotkey *hotkey = static_cast<const Hotkey *>(data);
	if (hotkey->command)
		Companion::hotkeyFromAnyThread(*hotkey->command);
	else
		Companion::automationHotkeyFromAnyThread(hotkey->automation);
}

} // namespace

void hotkeys_register()
{
	for (Hotkey &hotkey : hotkeys()) {
		if (hotkey.id == OBS_INVALID_HOTKEY_ID)
			hotkey.id = obs_hotkey_register_frontend(hotkey.name.c_str(), hotkey.description.c_str(),
								 pressed, &hotkey);
	}
}

void hotkeys_unregister()
{
	for (Hotkey &hotkey : hotkeys()) {
		if (hotkey.id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(hotkey.id);
		hotkey.id = OBS_INVALID_HOTKEY_ID;
	}
}

obs_data_t *hotkeys_save()
{
	obs_data_t *data = obs_data_create();
	for (const Hotkey &hotkey : hotkeys()) {
		if (hotkey.id == OBS_INVALID_HOTKEY_ID)
			continue;
		obs_data_array_t *keys = obs_hotkey_save(hotkey.id);
		obs_data_set_array(data, hotkey.name.c_str(), keys);
		obs_data_array_release(keys);
	}
	return data;
}

void hotkeys_load(obs_data_t *data)
{
	for (const Hotkey &hotkey : hotkeys()) {
		if (hotkey.id == OBS_INVALID_HOTKEY_ID)
			continue;
		obs_data_array_t *keys = data ? obs_data_get_array(data, hotkey.name.c_str()) : nullptr;
		if (!keys)
			keys = obs_data_array_create();
		obs_hotkey_load(hotkey.id, keys);
		obs_data_array_release(keys);
	}
}
