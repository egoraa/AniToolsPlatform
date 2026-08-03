#ifndef ATP_MCP_SOCKET_SERVER_HPP
#define ATP_MCP_SOCKET_SERVER_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/mcp/json_rpc.hpp>
#include <atp/runtime/socket_platform.hpp>

namespace atp::mcp {

/// A newline-delimited JSON endpoint on the loopback interface: the second transport of the protocol
/// atp_mcp already speaks over stdio, and the reason server::handle was written as a pure function.
///
/// One client at a time, on purpose. Multiplexing would raise a question nothing in scope answers —
/// what two clients editing one property mean — while the listen backlog already makes a second
/// connection wait rather than fail.
///
/// The channel is unauthenticated: binding to 127.0.0.1 keeps it off the network, but every local
/// process can reach it, since TCP has neither the file permissions of a socket file nor a portable
/// way to ask who the peer is. That is why a host must not open it unasked.
class socket_server {
   public:
    using handler = std::function<std::optional<nlohmann::json>(const nlohmann::json&)>;

    /// Binds, listens and starts serving on a thread of its own.
    /// @param port TCP port; 0 asks the OS for a free one, readable afterwards through port()
    /// @param on_message called on the server's thread for every decoded line; a nullopt reply is a
    ///        notification and nothing is written back
    /// @throws std::runtime_error if the socket cannot be created, bound or listened on
    socket_server(std::uint16_t port, handler on_message) : on_message_(std::move(on_message)) {
#if defined(_WIN32)
        WSADATA data;
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("cannot initialise Winsock");
        }
#endif
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ == runtime::detail::invalid_socket) {
            throw std::runtime_error("cannot create the control socket");
        }
        const int reuse = 1;
        (void)::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(listener_, 4) != 0) {
            runtime::detail::close_socket(listener_);
            listener_ = runtime::detail::invalid_socket;
            throw std::runtime_error("cannot listen on the control port");
        }
        runtime::detail::address_length_t length = sizeof(address);
        if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) == 0) {
            port_ = ntohs(address.sin_port);
        }
        worker_ = std::thread([this] { serve(); });
    }

    socket_server(const socket_server&) = delete;
    socket_server& operator=(const socket_server&) = delete;

    ~socket_server() {
        stop();
    }

    /// The port actually bound — the point of asking for 0.
    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

    /// Stops serving and closes everything; idempotent. Takes up to the poll timeout, because that
    /// is how the thread learns it should leave: closing the listening socket from another thread
    /// does not reliably wake a blocked accept, and a self-pipe would be more machinery than a
    /// bounded wait is worth.
    void stop() {
        stopping_.store(true, std::memory_order_relaxed);
        if (worker_.joinable()) {
            worker_.join();
        }
        if (listener_ != runtime::detail::invalid_socket) {
            runtime::detail::close_socket(listener_);
            listener_ = runtime::detail::invalid_socket;
#if defined(_WIN32)
            (void)::WSACleanup();
#endif
        }
    }

   private:
    static constexpr int poll_timeout_ms = 200;

    void serve() {
        while (!stopping_.load(std::memory_order_relaxed)) {
            if (runtime::detail::poll_readable(listener_, poll_timeout_ms) <= 0) {
                continue;
            }
            const runtime::detail::socket_t client = ::accept(listener_, nullptr, nullptr);
            if (client == runtime::detail::invalid_socket) {
                continue;
            }
#if defined(SO_NOSIGPIPE)
            const int nosigpipe = 1;
            (void)::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&nosigpipe),
                               sizeof(nosigpipe));
#endif
            serve_client(client);
            runtime::detail::close_socket(client);
        }
    }

    void serve_client(runtime::detail::socket_t client) {
        std::string buffer;
        while (!stopping_.load(std::memory_order_relaxed)) {
            if (runtime::detail::poll_readable(client, poll_timeout_ms) <= 0) {
                continue;
            }
            std::array<char, 4096> chunk;
            // auto, not int: recv answers int on Winsock and ssize_t on POSIX, so a cast that is
            // redundant on one platform is required on the other.
            const auto received = ::recv(client, chunk.data(), sizeof(chunk), 0);
            if (received <= 0) {
                return;
            }
            buffer.append(chunk.data(), static_cast<std::size_t>(received));
            for (std::size_t newline = buffer.find('\n'); newline != std::string::npos; newline = buffer.find('\n')) {
                const std::string line = buffer.substr(0, newline);
                buffer.erase(0, newline + 1);
                if (!line.empty() && !answer(client, line)) {
                    return;
                }
            }
        }
    }

    [[nodiscard]] bool answer(runtime::detail::socket_t client, const std::string& line) {
        std::optional<nlohmann::json> reply;
        try {
            reply = on_message_(nlohmann::json::parse(line));
        } catch (const nlohmann::json::parse_error& e) {
            reply = make_error(nullptr, rpc_parse_error, e.what());
        } catch (const std::exception& e) {
            reply = make_error(nullptr, rpc_internal_error, e.what());
        }
        if (!reply) {
            return true;
        }
        const std::string encoded = reply->dump() + "\n";
        std::size_t sent = 0;
        while (sent < encoded.size()) {
            const auto written = ::send(client, encoded.data() + sent, static_cast<int>(encoded.size() - sent),
                                        runtime::detail::send_flags);
            if (written <= 0) {
                return false;
            }
            sent += static_cast<std::size_t>(written);
        }
        return true;
    }

    handler on_message_;
    runtime::detail::socket_t listener_ = runtime::detail::invalid_socket;
    std::uint16_t port_ = 0;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

}  // namespace atp::mcp

#endif
