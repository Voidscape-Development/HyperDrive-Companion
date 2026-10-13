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

#include "engine-io.hpp"

using namespace hd::eio;

TEST(eio_packets)
{
	CHECK(parsePacket("2").type == PacketType::Ping);
	CHECK(parsePacket("3probe").type == PacketType::Pong);
	CHECK(parsePacket("").type == PacketType::Invalid);
	CHECK(parsePacket("x").type == PacketType::Invalid);
	Packet message = parsePacket("42[\"a\",1]");
	CHECK(message.type == PacketType::Message);
	CHECK_EQ(message.data, "2[\"a\",1]");
}

TEST(eio_handshake)
{
	Packet open = parsePacket(
		R"(0{"sid":"abc","upgrades":[],"pingInterval":25000,"pingTimeout":5000,"maxPayload":1000000})");
	CHECK(open.type == PacketType::Open);
	auto handshake = parseHandshake(open.data);
	CHECK(handshake.has_value());
	CHECK_EQ(handshake->sid, "abc");
	CHECK_EQ(handshake->pingTimeout, 5000);
	CHECK(!parseHandshake("nope").has_value());
}

TEST(sio_event)
{
	SocketPacket packet = parseSocketPacket(R"(2["program_state",{"state":{"a":1},"delta_index":3}])");
	CHECK(packet.type == SocketType::Event);
	CHECK_EQ(packet.nsp, "/");
	auto event = toEvent(packet);
	CHECK(event.has_value());
	CHECK_EQ(event->name, "program_state");
	CHECK_EQ(event->data["delta_index"].get<int>(), 3);
}

TEST(sio_namespace_and_ack)
{
	SocketPacket packet = parseSocketPacket(R"(2/admin,12["hello"])");
	CHECK(packet.type == SocketType::Event);
	CHECK_EQ(packet.nsp, "/admin");
	CHECK(packet.id.has_value());
	CHECK_EQ(*packet.id, 12LL);
	auto event = toEvent(packet);
	CHECK(event.has_value());
	CHECK(event->data.is_null());

	SocketPacket connect = parseSocketPacket(R"(0{"sid":"x"})");
	CHECK(connect.type == SocketType::Connect);
	CHECK(parseSocketPacket("0").type == SocketType::Connect);
	CHECK(parseSocketPacket("2[broken").type == SocketType::Invalid);
	CHECK(!toEvent(parseSocketPacket("2{}")).has_value());
}

TEST(sio_encode)
{
	CHECK_EQ(encodeConnect(), "40");
	CHECK_EQ(encodePong(), "3");
	CHECK_EQ(encodeEvent("program_state", nlohmann::json::object()), R"(42["program_state",{}])");
}
