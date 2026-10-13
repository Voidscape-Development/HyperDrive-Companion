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
#include "test.hpp"

#include "address.hpp"

using namespace hd;

TEST(address_defaults)
{
	Address a = parseAddress("", 0);
	CHECK_EQ(a.host, "localhost");
	CHECK_EQ(a.port, 5500);
}

TEST(address_typed_forms)
{
	CHECK_EQ(parseAddress(" 192.168.1.5 ", 5500).host, "192.168.1.5");
	Address url = parseAddress("http://pc.local:5501/scoreboard", 5500);
	CHECK_EQ(url.host, "pc.local");
	CHECK_EQ(url.port, 5501);
	// A port typed with the host wins
	CHECK_EQ(parseAddress("pc:6000", 5500).port, 6000);
	CHECK_EQ(parseAddress("pc", 7000).port, 7000);
	CHECK_EQ(parseAddress("pc", 70000).port, 5500);
}

TEST(address_ipv6)
{
	Address bare = parseAddress("::1", 5500);
	CHECK_EQ(bare.host, "::1");
	CHECK_EQ(baseUrl(bare), "http://[::1]:5500");
	Address bracketed = parseAddress("[fe80::1]:5600", 5500);
	CHECK_EQ(bracketed.host, "fe80::1");
	CHECK_EQ(bracketed.port, 5600);
	CHECK_EQ(websocketUrl(bracketed), "ws://[fe80::1]:5600");
}

TEST(url_encode)
{
	CHECK_EQ(urlEncode("TV 1"), "TV%201");
	CHECK_EQ(urlEncode("a-b_c.d~"), "a-b_c.d~");
	CHECK_EQ(urlEncode("é/&"), "%C3%A9%2F%26");
}
