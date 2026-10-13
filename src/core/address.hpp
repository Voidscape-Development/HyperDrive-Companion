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

#include <string>

namespace hd {

/** Where HyperDrive's web server is: localhost:5500 by default. */
struct Address {
	std::string host = "localhost";
	int port = 5500;

	bool operator==(const Address &other) const { return host == other.host && port == other.port; }
	bool operator!=(const Address &other) const { return !(*this == other); }
};

/**
 * Normalizes what a user typed as HyperDrive's address: "192.168.1.5",
 * "http://pc.local:5500/" or "pc:5501" (a port typed with the host wins).
 */
Address parseAddress(const std::string &host, int port);

/** "http://host:port", with brackets around an IPv6 host. */
std::string baseUrl(const Address &address);

/** "ws://host:port" */
std::string websocketUrl(const Address &address);

/** Percent-encodes everything but RFC 3986's unreserved characters. */
std::string urlEncode(const std::string &value);

} // namespace hd
