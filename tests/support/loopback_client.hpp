// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_TESTS_SUPPORT_LOOPBACK_CLIENT_HPP
#define ATP_TESTS_SUPPORT_LOOPBACK_CLIENT_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace atp_tests {

/// A blocking line client for the socket tests: the one place in the test tree with networking
/// ifdefs, so a test reads as a conversation and not as platform code.
class loopback_client {
   public:
    /// Connects to 127.0.0.1 on the given port.
    /// @throws std::runtime_error if the connection is refused
    explicit loopback_client(std::uint16_t port) {
#if defined(_WIN32)
        WSADATA data;
        (void)::WSAStartup(MAKEWORD(2, 2), &data);
#endif
        socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            throw std::runtime_error("cannot connect to the control channel");
        }
    }

    loopback_client(const loopback_client&) = delete;
    loopback_client& operator=(const loopback_client&) = delete;

    ~loopback_client() {
#if defined(_WIN32)
        (void)::closesocket(socket_);
        (void)::WSACleanup();
#else
        (void)::close(socket_);
#endif
    }

    /// Writes one line and blocks until a whole line comes back.
    /// @param line request without its newline
    /// @return the reply without its newline; empty if the peer closed first
    [[nodiscard]] std::string exchange(const std::string& line) {
        const std::string out = line + "\n";
        (void)::send(socket_, out.data(), static_cast<int>(out.size()), 0);
        std::string reply;
        char chunk[1024];
        while (reply.find('\n') == std::string::npos) {
            const int received = static_cast<int>(::recv(socket_, chunk, sizeof(chunk), 0));
            if (received <= 0) {
                return {};
            }
            reply.append(chunk, static_cast<std::size_t>(received));
        }
        return reply.substr(0, reply.find('\n'));
    }

   private:
#if defined(_WIN32)
    SOCKET socket_ = INVALID_SOCKET;
#else
    int socket_ = -1;
#endif
};

}  // namespace atp_tests

#endif
