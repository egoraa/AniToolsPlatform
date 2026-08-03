#ifndef ATP_STUDIO_REMOTE_CLIENT_HPP
#define ATP_STUDIO_REMOTE_CLIENT_HPP

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/runtime/socket_platform.hpp>

namespace atp::studio {

/// Failure of a remote call: a socket that would not connect or died, a reply that did not arrive
/// within the timeout, or a tool that answered isError. One type for all three on purpose — the
/// caller does the same thing with each, which is to say what happened and stop trusting the view.
class remote_error : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

/// Synchronous MCP client over a loopback socket: the other end of atp_app's control channel.
///
/// Synchronous with a timeout, not asynchronous with a thread. The GUI polls a few times a second,
/// and a second concurrency design — a worker plus a queue back onto the GUI thread — would cost
/// more than it saves: the worst case here is one wait of the timeout, after which the caller
/// detaches.
class remote_client {
   public:
    /// Connects immediately.
    /// @param host host name or IPv4 literal
    /// @param port TCP port
    /// @param timeout how long any single call may wait for its reply
    /// @throws remote_error if the endpoint cannot be resolved or refuses the connection
    remote_client(std::string host, std::uint16_t port, std::chrono::milliseconds timeout)
        : host_(std::move(host)), port_(port), timeout_(timeout), endpoint_(host_ + ":" + std::to_string(port_)) {
        connect();
    }

    remote_client(const remote_client&) = delete;
    remote_client& operator=(const remote_client&) = delete;

    ~remote_client() {
        close();
#if defined(_WIN32)
        if (winsock_) {
            (void)::WSACleanup();
        }
#endif
    }

    /// Calls a tool.
    /// @param tool tool name, as tools/list reports it
    /// @param arguments the "arguments" object; an empty object for a tool that takes none
    /// @return the tool's structuredContent
    /// @throws remote_error on a transport failure, which also disconnects the client, since a
    ///         half-read stream cannot be trusted for the next call; and on an isError reply, which
    ///         leaves the connection alone — the remote simply refused this one thing
    [[nodiscard]] nlohmann::json call(const std::string& tool,
                                      const nlohmann::json& arguments = nlohmann::json::object()) {
        const nlohmann::json request{{"jsonrpc", "2.0"},
                                     {"id", ++id_},
                                     {"method", "tools/call"},
                                     {"params", {{"name", tool}, {"arguments", arguments}}}};
        const nlohmann::json reply = exchange(request);
        if (reply.contains("error")) {
            fail("the remote reported " + reply.at("error").value("message", std::string("an error")));
        }
        if (!reply.contains("result")) {
            fail("the remote sent a reply with neither a result nor an error");
        }
        const nlohmann::json& result = reply.at("result");
        if (result.value("isError", false)) {
            std::string text = "the remote refused '" + tool + "'";
            for (const nlohmann::json& c : result.value("content", nlohmann::json::array())) {
                text += ": " + c.value("text", std::string());
            }
            throw remote_error(text);
        }
        return result.value("structuredContent", nlohmann::json::object());
    }

    /// Whether the socket is still usable.
    [[nodiscard]] bool connected() const {
        return socket_ != runtime::detail::invalid_socket;
    }

    /// "host:port", for the messages a person reads.
    [[nodiscard]] const std::string& endpoint() const {
        return endpoint_;
    }

    /// Changes the reply timeout: polling wants a short one, a user's action a longer one.
    void set_timeout(std::chrono::milliseconds timeout) {
        timeout_ = timeout;
    }

   private:
    void connect() {
#if defined(_WIN32)
        WSADATA data;
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw remote_error("cannot initialise Winsock");
        }
        winsock_ = true;
#endif
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* resolved = nullptr;
        const std::string service = std::to_string(port_);
        if (::getaddrinfo(host_.c_str(), service.c_str(), &hints, &resolved) != 0 || resolved == nullptr) {
            throw remote_error("cannot resolve '" + endpoint_ + "'");
        }
        socket_ = ::socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
        const bool ok = socket_ != runtime::detail::invalid_socket &&
                        ::connect(socket_, resolved->ai_addr,
                                  static_cast<runtime::detail::address_length_t>(resolved->ai_addrlen)) == 0;
        ::freeaddrinfo(resolved);
        if (!ok) {
            close();
            throw remote_error("cannot connect to '" + endpoint_ + "'");
        }
    }

    void close() {
        if (socket_ != runtime::detail::invalid_socket) {
            runtime::detail::close_socket(socket_);
            socket_ = runtime::detail::invalid_socket;
        }
    }

    [[noreturn]] void fail(const std::string& text) {
        close();
        throw remote_error(text + " (" + endpoint_ + ")");
    }

    [[nodiscard]] nlohmann::json exchange(const nlohmann::json& request) {
        if (!connected()) {
            throw remote_error("not connected to '" + endpoint_ + "'");
        }
        const std::string encoded = request.dump() + "\n";
        std::size_t sent = 0;
        while (sent < encoded.size()) {
            // auto, not int: send and recv answer int on Winsock and ssize_t on POSIX, so a cast
            // that is redundant on one platform is required on the other.
            const auto written = ::send(socket_, encoded.data() + sent, static_cast<int>(encoded.size() - sent),
                                        runtime::detail::send_flags);
            if (written <= 0) {
                fail("the control channel closed while sending");
            }
            sent += static_cast<std::size_t>(written);
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout_;
        while (!buffer_.contains('\n')) {
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0 || runtime::detail::poll_readable(socket_, static_cast<int>(left.count())) <= 0) {
                fail("the remote did not answer within the timeout");
            }
            std::array<char, 4096> chunk;
            const auto received = ::recv(socket_, chunk.data(), sizeof(chunk), 0);
            if (received <= 0) {
                fail("the control channel closed while receiving");
            }
            buffer_.append(chunk.data(), static_cast<std::size_t>(received));
        }
        const std::size_t newline = buffer_.find('\n');
        const std::string line = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        try {
            return nlohmann::json::parse(line);
        } catch (const nlohmann::json::parse_error&) {
            fail("the remote sent something that is not JSON");
        }
    }

    std::string host_;
    std::uint16_t port_;
    std::chrono::milliseconds timeout_;
    std::string endpoint_;
    runtime::detail::socket_t socket_ = runtime::detail::invalid_socket;
    std::string buffer_;
    std::uint64_t id_ = 0;
#if defined(_WIN32)
    /// Whether WSAStartup succeeded. The counter it bumps is per process, so the matching cleanup
    /// belongs to the destructor and not to close(), which also runs on the failed-connect path.
    bool winsock_ = false;
#endif
};

}  // namespace atp::studio

#endif
