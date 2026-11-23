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

#include <QString>

#include <string>

#include <obs-module.h>

#include "core/config.hpp"

/** A translated text of data/locale */
inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

inline QString T(const std::string &key)
{
	return T(key.c_str());
}

/** "A set is won", "Scene on program is", "Switch scene"... */
inline QString kindText(hd::TriggerKind kind)
{
	return T(std::string("HyperDrive.Trigger.") + hd::idOf(kind));
}
inline QString kindText(hd::ConditionKind kind)
{
	return T(std::string("HyperDrive.Condition.") + hd::idOf(kind));
}
inline QString kindText(hd::ActionKind kind)
{
	return T(std::string("HyperDrive.Action.") + hd::idOf(kind));
}
