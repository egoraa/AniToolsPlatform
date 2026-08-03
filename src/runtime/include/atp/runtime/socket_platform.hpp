#ifndef ATP_RUNTIME_SOCKET_PLATFORM_HPP
#define ATP_RUNTIME_SOCKET_PLATFORM_HPP

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace atp::runtime::detail {

/// Platform sockets in one place: the types and calls that differ between Winsock and POSIX, so that
/// the ones who open a socket carry no ifdefs of their own.
///
/// It lives in the runtime rather than beside the MCP server because the studio's client needs the
/// same names, and atp_mcp_lib already links atp_studio_lib — a header both sides need has to sit
/// below both, or the dependency turns into a cycle. Everything here is deliberately thin: it
/// renames, it does not wrap.
#if defined(_WIN32)
using socket_t = SOCKET;
using address_length_t = int;
inline constexpr socket_t invalid_socket = INVALID_SOCKET;
inline constexpr int send_flags = 0;

inline void close_socket(socket_t s) {
    (void)::closesocket(s);
}

inline int poll_readable(socket_t s, int timeout_ms) {
    WSAPOLLFD fd{s, POLLRDNORM, 0};
    return ::WSAPoll(&fd, 1, timeout_ms);
}
#else
using socket_t = int;
using address_length_t = socklen_t;
inline constexpr socket_t invalid_socket = -1;
#if defined(MSG_NOSIGNAL)
inline constexpr int send_flags = MSG_NOSIGNAL;
#else
inline constexpr int send_flags = 0;
#endif

inline void close_socket(socket_t s) {
    (void)::close(s);
}

inline int poll_readable(socket_t s, int timeout_ms) {
    pollfd fd{s, POLLIN, 0};
    return ::poll(&fd, 1, timeout_ms);
}
#endif

}  // namespace atp::runtime::detail

#endif
