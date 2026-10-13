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
 * The parts of Engine.IO 4 and Socket.IO 5 the plugin needs, over a plain
 * WebSocket (no long-polling, no binary packets): HyperDrive's web server is
 * python-socketio. A WebSocket frame is one Engine.IO packet:
 *
 *   0{"sid":..,"pingInterval":25000,"pingTimeout":20000}   open
 *   2 / 3                                                   ping / pong
 *   4<socket.io packet>                                     message
 *
 * and a Socket.IO packet is a type digit, an optional "/namespace," an
 * optional ack id and JSON: 40 (connect), 42["event",data] (event).
 */

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace hd::eio {

/** The path to open the WebSocket on */
constexpr const char *kSocketPath = "/socket.io/?EIO=4&transport=websocket";

enum class PacketType { Open, Close, Ping, Pong, Message, Upgrade, Noop, Invalid };

struct Packet {
	PacketType type = PacketType::Invalid;
	std::string data;
};

Packet parsePacket(const std::string &frame);

struct Handshake {
	std::string sid;
	int pingInterval = 25000;
	int pingTimeout = 20000;
};

std::optional<Handshake> parseHandshake(const std::string &data);

enum class SocketType { Connect = 0, Disconnect, Event, Ack, ConnectError, BinaryEvent, BinaryAck, Invalid };

struct SocketPacket {
	SocketType type = SocketType::Invalid;
	std::string nsp = "/";
	std::optional<long long> id;
	nlohmann::json data;
};

/** A Socket.IO packet, from a Message's data */
SocketPacket parseSocketPacket(const std::string &data);

/** An event's name and its first argument (null if none), if it's an event */
struct Event {
	std::string name;
	nlohmann::json data;
};
std::optional<Event> toEvent(const SocketPacket &packet);

std::string encodePong();
/** Connects to the main namespace */
std::string encodeConnect();
std::string encodeEvent(const std::string &name, const nlohmann::json &data);

} // namespace hd::eio
