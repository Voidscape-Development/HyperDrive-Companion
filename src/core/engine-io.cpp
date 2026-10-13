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

#include "engine-io.hpp"

#include <cctype>

namespace hd::eio {

Packet parsePacket(const std::string &frame)
{
	Packet packet;
	if (frame.empty())
		return packet;
	switch (frame[0]) {
	case '0':
		packet.type = PacketType::Open;
		break;
	case '1':
		packet.type = PacketType::Close;
		break;
	case '2':
		packet.type = PacketType::Ping;
		break;
	case '3':
		packet.type = PacketType::Pong;
		break;
	case '4':
		packet.type = PacketType::Message;
		break;
	case '5':
		packet.type = PacketType::Upgrade;
		break;
	case '6':
		packet.type = PacketType::Noop;
		break;
	default:
		return packet;
	}
	packet.data = frame.substr(1);
	return packet;
}

std::optional<Handshake> parseHandshake(const std::string &data)
{
	nlohmann::json json = nlohmann::json::parse(data, nullptr, false);
	if (!json.is_object())
		return std::nullopt;
	Handshake handshake;
	if (json.contains("sid") && json["sid"].is_string())
		handshake.sid = json["sid"].get<std::string>();
	if (json.contains("pingInterval") && json["pingInterval"].is_number_integer())
		handshake.pingInterval = json["pingInterval"].get<int>();
	if (json.contains("pingTimeout") && json["pingTimeout"].is_number_integer())
		handshake.pingTimeout = json["pingTimeout"].get<int>();
	return handshake;
}

SocketPacket parseSocketPacket(const std::string &data)
{
	SocketPacket packet;
	if (data.empty() || data[0] < '0' || data[0] > '6')
		return packet;
	SocketType type = static_cast<SocketType>(data[0] - '0');

	size_t pos = 1;
	// Binary packets have "<attachments>-" first: not supported
	if (type == SocketType::BinaryEvent || type == SocketType::BinaryAck) {
		packet.type = type;
		return packet;
	}
	if (pos < data.size() && data[pos] == '/') {
		size_t comma = data.find(',', pos);
		if (comma == std::string::npos) {
			packet.nsp = data.substr(pos);
			pos = data.size();
		} else {
			packet.nsp = data.substr(pos, comma - pos);
			pos = comma + 1;
		}
	}
	size_t idStart = pos;
	while (pos < data.size() && std::isdigit(static_cast<unsigned char>(data[pos])))
		pos++;
	if (pos > idStart && pos - idStart < 18)
		packet.id = std::stoll(data.substr(idStart, pos - idStart));

	if (pos < data.size()) {
		packet.data = nlohmann::json::parse(data.begin() + static_cast<std::ptrdiff_t>(pos), data.end(),
						    nullptr, false);
		if (packet.data.is_discarded())
			return packet;
	}
	packet.type = type;
	return packet;
}

std::optional<Event> toEvent(const SocketPacket &packet)
{
	if (packet.type != SocketType::Event || !packet.data.is_array() || packet.data.empty() ||
	    !packet.data[0].is_string())
		return std::nullopt;
	Event event;
	event.name = packet.data[0].get<std::string>();
	event.data = packet.data.size() > 1 ? packet.data[1] : nlohmann::json();
	return event;
}

std::string encodePong()
{
	return "3";
}

std::string encodeConnect()
{
	return "40";
}

std::string encodeEvent(const std::string &name, const nlohmann::json &data)
{
	return "42" + nlohmann::json::array({name, data}).dump();
}

} // namespace hd::eio
