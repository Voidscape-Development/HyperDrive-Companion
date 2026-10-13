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
 * The plugin's one connection to HyperDrive: socket.io for the state, as the
 * layouts and the Stream Deck plugin do, and the web server's links for
 * commands (their answers tell a refused command apart).
 *
 * Callbacks run on the client's network threads: the plugin hands them to
 * the UI thread itself.
 */

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "core/address.hpp"
#include "core/commands.hpp"

namespace ix {
class WebSocket;
class HttpClient;
} // namespace ix

namespace hd {

enum class ConnectionStatus { Disconnected, Connecting, Connected };

struct CommandResult {
	bool ok = false;
	/** 0 when HyperDrive couldn't be reached */
	int status = 0;
	std::string body;
};

class HyperDriveClient {
public:
	using StateCallback = std::function<void()>;
	using StatusCallback = std::function<void(ConnectionStatus)>;
	using ResultCallback = std::function<void(const CommandResult &)>;

	HyperDriveClient();
	~HyperDriveClient();

	HyperDriveClient(const HyperDriveClient &) = delete;
	HyperDriveClient &operator=(const HyperDriveClient &) = delete;

	/** Called whenever the state changes (often: coalesce it) */
	void setStateCallback(StateCallback callback);
	void setStatusCallback(StatusCallback callback);

	/** Connects, or reconnects if the address changed. Reconnects by itself. */
	void connect(const Address &address);
	void disconnect();

	Address address() const;
	ConnectionStatus status() const { return status_.load(); }

	/** A copy of the whole state */
	nlohmann::json state() const;
	/** A copy of part of the state ("score"), or null */
	nlohmann::json statePart(const std::string &key) const;
	/** Runs fn with the state, locked: keep it short */
	void withState(const std::function<void(const nlohmann::json &)> &fn) const;

	/** Calls one of the web server's links in the background */
	void command(const Request &request, ResultCallback callback = {});

	/** Calls a link and waits for its answer (for the layout list): not on a hot path */
	CommandResult commandSync(const Request &request, int timeoutSecs = 2);

private:
	void onMessage(const std::string &text);
	void onEvent(const std::string &name, const nlohmann::json &data);
	void send(const std::string &text);
	void setStatus(ConnectionStatus status);
	void notifyState();

	mutable std::mutex stateMutex_;
	nlohmann::json state_ = nlohmann::json::object();
	std::optional<long long> deltaIndex_;

	mutable std::mutex mutex_;
	Address address_;
	std::unique_ptr<ix::WebSocket> socket_;
	std::unique_ptr<ix::HttpClient> http_;
	StateCallback stateCallback_;
	StatusCallback statusCallback_;
	std::atomic<ConnectionStatus> status_{ConnectionStatus::Disconnected};
};

} // namespace hd
