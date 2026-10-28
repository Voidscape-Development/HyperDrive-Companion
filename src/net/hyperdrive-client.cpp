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
#include "hyperdrive-client.hpp"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXWebSocket.h>

#include "core/engine-io.hpp"
#include "core/state.hpp"

namespace hd {

namespace {

ix::HttpRequestArgsPtr requestArgs(ix::HttpClient &client, const std::string &url, const Request &request,
				   int timeoutSecs)
{
	ix::HttpRequestArgsPtr args = client.createRequest(url, request.method);
	args->connectTimeout = timeoutSecs;
	args->transferTimeout = timeoutSecs + 3;
	args->followRedirects = true;
	args->compress = false;
	if (request.body) {
		args->body = request.body->dump();
		args->extraHeaders["Content-Type"] = "application/json";
	}
	return args;
}

CommandResult toResult(const ix::HttpResponsePtr &response)
{
	CommandResult result;
	if (!response || response->errorCode != ix::HttpErrorCode::Ok) {
		result.body = response ? response->errorMsg : "No response";
		return result;
	}
	result.status = response->statusCode;
	result.body = response->body;
	result.ok = commandSucceeded(result.status, result.body);
	return result;
}

} // namespace

HyperDriveClient::HyperDriveClient() : http_(std::make_unique<ix::HttpClient>(true)) {}

HyperDriveClient::~HyperDriveClient()
{
	disconnect();
	std::lock_guard<std::mutex> lock(mutex_);
	stateCallback_ = nullptr;
	statusCallback_ = nullptr;
	http_.reset();
}

void HyperDriveClient::setStateCallback(StateCallback callback)
{
	std::lock_guard<std::mutex> lock(mutex_);
	stateCallback_ = std::move(callback);
}

void HyperDriveClient::setStatusCallback(StatusCallback callback)
{
	std::lock_guard<std::mutex> lock(mutex_);
	statusCallback_ = std::move(callback);
}

Address HyperDriveClient::address() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return address_;
}

void HyperDriveClient::connect(const Address &address)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (socket_ && address == address_)
			return;
	}
	disconnect();

	auto socket = std::make_unique<ix::WebSocket>();
	socket->setUrl(websocketUrl(connectAddress(address)) + eio::kSocketPath);
	socket->enableAutomaticReconnection();
	socket->setMinWaitBetweenReconnectionRetries(1000);
	socket->setMaxWaitBetweenReconnectionRetries(5000);
	socket->setHandshakeTimeout(5);
	// Engine.IO has its own heartbeat (the server's "2", answered "3"); a
	// WebSocket ping as well finds a dead connection when the server is gone
	socket->setPingInterval(20);
	socket->setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
		switch (msg->type) {
		case ix::WebSocketMessageType::Open:
			// Connected once socket.io answers the namespace connect
			setStatus(ConnectionStatus::Connecting);
			break;
		case ix::WebSocketMessageType::Message:
			if (!msg->binary)
				onMessage(msg->str);
			break;
		case ix::WebSocketMessageType::Close: {
			std::lock_guard<std::mutex> lock(stateMutex_);
			deltaIndex_.reset();
		}
			setStatus(ConnectionStatus::Connecting);
			break;
		case ix::WebSocketMessageType::Error:
			setStatus(ConnectionStatus::Disconnected);
			break;
		default:
			break;
		}
	});

	{
		std::lock_guard<std::mutex> lock(mutex_);
		address_ = address;
		socket_ = std::move(socket);
	}
	setStatus(ConnectionStatus::Connecting);
	std::lock_guard<std::mutex> lock(mutex_);
	socket_->start();
}

void HyperDriveClient::disconnect()
{
	std::unique_ptr<ix::WebSocket> socket;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		socket = std::move(socket_);
	}
	if (socket)
		socket->stop();
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		state_ = nlohmann::json::object();
		deltaIndex_.reset();
	}
	setStatus(ConnectionStatus::Disconnected);
	notifyState();
}

nlohmann::json HyperDriveClient::state() const
{
	std::lock_guard<std::mutex> lock(stateMutex_);
	return state_;
}

nlohmann::json HyperDriveClient::statePart(const std::string &key) const
{
	std::lock_guard<std::mutex> lock(stateMutex_);
	auto it = state_.find(key);
	return it == state_.end() ? nlohmann::json() : *it;
}

void HyperDriveClient::withState(const std::function<void(const nlohmann::json &)> &fn) const
{
	std::lock_guard<std::mutex> lock(stateMutex_);
	fn(state_);
}

void HyperDriveClient::send(const std::string &text)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (socket_)
		socket_->sendText(text);
}

void HyperDriveClient::onMessage(const std::string &text)
{
	eio::Packet packet = eio::parsePacket(text);
	switch (packet.type) {
	case eio::PacketType::Open:
		send(eio::encodeConnect());
		return;
	case eio::PacketType::Ping:
		send(eio::encodePong());
		return;
	case eio::PacketType::Message:
		break;
	default:
		return;
	}

	eio::SocketPacket socketPacket = eio::parseSocketPacket(packet.data);
	if (socketPacket.nsp != "/")
		return;
	switch (socketPacket.type) {
	case eio::SocketType::Connect:
		// HyperDrive sends the whole state right after
		setStatus(ConnectionStatus::Connected);
		break;
	case eio::SocketType::ConnectError:
	case eio::SocketType::Disconnect:
		setStatus(ConnectionStatus::Disconnected);
		break;
	case eio::SocketType::Event:
		if (auto event = eio::toEvent(socketPacket))
			onEvent(event->name, event->data);
		break;
	default:
		break;
	}
}

void HyperDriveClient::onEvent(const std::string &name, const nlohmann::json &data)
{
	if (name == "program_state") {
		// Sent on connect, after a big change, or when asked for
		{
			std::lock_guard<std::mutex> lock(stateMutex_);
			auto state = data.is_object() ? data.find("state") : data.end();
			state_ = state != data.end() && state->is_object() ? *state : nlohmann::json::object();
			auto index = data.is_object() ? data.find("delta_index") : data.end();
			if (index != data.end() && index->is_number_integer())
				deltaIndex_ = index->get<long long>();
			else
				deltaIndex_.reset();
		}
		notifyState();
		return;
	}
	if (name != "program_state_update" || !data.is_object())
		return;

	auto index = data.find("delta_index");
	auto delta = data.find("delta");
	if (index == data.end() || !index->is_number_integer() || delta == data.end() || !delta->is_array())
		return;
	long long deltaIndex = index->get<long long>();

	bool resync = false;
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (deltaIndex_ && deltaIndex <= *deltaIndex_) {
			// Already in the state we have
			return;
		}
		if (deltaIndex_ && deltaIndex != *deltaIndex_ + 1) {
			// Missed an update: get the whole state again
			resync = true;
		} else if (applyDeltas(state_, *delta)) {
			deltaIndex_ = deltaIndex;
		} else {
			resync = true;
		}
		if (resync)
			deltaIndex_.reset();
	}
	if (resync) {
		send(eio::encodeEvent("program_state", nlohmann::json::object()));
		return;
	}
	notifyState();
}

void HyperDriveClient::setStatus(ConnectionStatus status)
{
	if (status_.exchange(status) == status)
		return;
	StatusCallback callback;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		callback = statusCallback_;
	}
	if (callback)
		callback(status);
}

void HyperDriveClient::notifyState()
{
	StateCallback callback;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		callback = stateCallback_;
	}
	if (callback)
		callback();
}

void HyperDriveClient::command(const Request &request, ResultCallback callback)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!http_)
		return;
	std::string url = baseUrl(connectAddress(address_)) + request.path;
	ix::HttpRequestArgsPtr args = requestArgs(*http_, url, request, 3);
	http_->performRequest(args, [callback](const ix::HttpResponsePtr &response) {
		if (callback)
			callback(toResult(response));
	});
}

CommandResult HyperDriveClient::commandSync(const Request &request, int timeoutSecs)
{
	std::string url = baseUrl(connectAddress(address())) + request.path;
	ix::HttpClient client(false);
	ix::HttpRequestArgsPtr args = requestArgs(client, url, request, timeoutSecs);
	return toResult(client.request(url, request.method, args->body, args));
}

} // namespace hd
