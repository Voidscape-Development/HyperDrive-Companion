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
 * The "HyperDrive Bracket Focus" source: HyperDrive's bracket_focus layout,
 * drawn by OBS itself instead of a browser. The bracket is placed by
 * HyperDrive (bracket.channels.<channel>.placement); each set, round name
 * and line is a picture painted by a thread of the source's own at the
 * scale the camera needs, and the camera's moves and the fades run on OBS's
 * graphics thread, every frame, whatever the painting is doing.
 */

void bracket_source_register();
/** Stops the sources' threads, before the plugin's hub goes away */
void bracket_source_shutdown();
