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

#include <obs.h>

/*
 * OBS hotkeys for what the dock does. The scoreboard ones control the
 * scoreboard picked in the settings (saved with the scene collection, as
 * are the keys).
 */

void hotkeys_register();
void hotkeys_unregister();
/** The keys of every hotkey, for the scene collection */
obs_data_t *hotkeys_save();
void hotkeys_load(obs_data_t *data);
