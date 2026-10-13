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

#include "address.hpp"

#include <cctype>

namespace hd {

namespace {

std::string trim(const std::string &s)
{
	size_t start = 0;
	size_t end = s.size();
	while (start < end && std::isspace(static_cast<unsigned char>(s[start])))
		start++;
	while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
		end--;
	return s.substr(start, end - start);
}

bool allDigits(const std::string &s)
{
	if (s.empty())
		return false;
	for (char c : s) {
		if (!std::isdigit(static_cast<unsigned char>(c)))
			return false;
	}
	return true;
}

int toPort(const std::string &digits)
{
	if (!allDigits(digits) || digits.size() > 5)
		return 0;
	return std::stoi(digits);
}

std::string urlHost(const std::string &host)
{
	// IPv6 addresses need brackets in URLs
	if (host.find(':') != std::string::npos && host.front() != '[')
		return "[" + host + "]";
	return host;
}

} // namespace

Address parseAddress(const std::string &hostInput, int port)
{
	std::string h = trim(hostInput);

	// Drop a scheme ("http://") and a path ("/scoreboard")
	size_t scheme = h.find("://");
	if (scheme != std::string::npos)
		h = h.substr(scheme + 3);
	size_t slash = h.find('/');
	if (slash != std::string::npos)
		h = h.substr(0, slash);

	// "host:port", but not a bare IPv6 address (several colons)
	size_t colon = h.rfind(':');
	if (colon != std::string::npos && h.find(':') == colon) {
		int typed = toPort(h.substr(colon + 1));
		if (typed > 0) {
			h = h.substr(0, colon);
			port = typed;
		}
	}
	// "[::1]:5500"
	if (!h.empty() && h.front() == '[') {
		size_t close = h.find(']');
		if (close != std::string::npos) {
			if (close + 1 < h.size() && h[close + 1] == ':') {
				int typed = toPort(h.substr(close + 2));
				if (typed > 0)
					port = typed;
			}
			h = h.substr(1, close - 1);
		}
	}

	Address address;
	if (!h.empty())
		address.host = h;
	if (port > 0 && port < 65536)
		address.port = port;
	return address;
}

std::string baseUrl(const Address &address)
{
	return "http://" + urlHost(address.host) + ":" + std::to_string(address.port);
}

std::string websocketUrl(const Address &address)
{
	return "ws://" + urlHost(address.host) + ":" + std::to_string(address.port);
}

std::string urlEncode(const std::string &value)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(value.size() * 3);
	for (unsigned char c : value) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0x0F];
		}
	}
	return out;
}

} // namespace hd
