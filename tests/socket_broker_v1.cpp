#include "atto/socket_broker_v1.hpp"
#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

using namespace atto::security;
namespace {
struct TcpPair { int adopted = -1; int peer = -1; };
void close_if_valid(int fd) { if (fd >= 0) (void)::close(fd); }
TcpPair pair_tcp() {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    assert(::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    assert(::listen(listener, 1) == 0);
    socklen_t len = sizeof(addr);
    assert(::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    const int client = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(client >= 0);
    assert(::connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    const int server = ::accept(listener, nullptr, nullptr);
    assert(server >= 0);
    close_if_valid(listener);
    return {server, client};
}
std::span<const std::byte> bytes(const char* text, std::size_t n) {
    return {reinterpret_cast<const std::byte*>(text), n};
}
}  // namespace

int main() {
    SocketBroker broker;
    auto a = broker.start_tenant(1, {1, 4});
    auto b = broker.start_tenant(2, {1, 16});
    assert(a && b);
    assert(!broker.start_tenant(1, {4, 4096}));
    auto ca = pair_tcp();
    auto cb = pair_tcp();
    auto ax = broker.adopt_connected_tcp(*a, ca.adopted,
        rights(SocketRight::read) | rights(SocketRight::close));
    auto by = broker.adopt_connected_tcp(*b, cb.adopted,
        rights(SocketRight::write) | rights(SocketRight::close));
    assert(ax.status == SocketStatus::ok && by.status == SocketStatus::ok);
    assert((::fcntl(ca.adopted, F_GETFL, 0) & O_NONBLOCK) != 0);
    assert((::fcntl(cb.adopted, F_GETFD, 0) & FD_CLOEXEC) != 0);
    std::array<std::byte, 4> received{};
    assert(::send(ca.peer, "ping", 4, 0) == 4);
    auto n = broker.read(*a, ax.handle, received);
    assert(n.status == SocketStatus::ok && n.bytes == 4);
    assert(std::memcmp(received.data(), "ping", 4) == 0);
    assert(broker.read(*b, ax.handle, received).status == SocketStatus::denied);
    assert(broker.read(*a, by.handle, received).status == SocketStatus::denied);
    assert(broker.write(*a, ax.handle, bytes("x", 1)).status == SocketStatus::denied);
    assert(broker.close(*a, by.handle) == SocketStatus::denied);
    assert(broker.read(*a, ax.handle, received).status == SocketStatus::quota);
    assert(broker.write(*b, by.handle, bytes("pong", 4)).status == SocketStatus::ok);
    char from_b[4]{};
    assert(::recv(cb.peer, from_b, sizeof(from_b), 0) == 4);
    assert(std::memcmp(from_b, "pong", 4) == 0);
    auto quota_socket = pair_tcp();
    assert(broker.adopt_connected_tcp(*b, quota_socket.adopted,
        rights(SocketRight::read)).status == SocketStatus::quota);
    // On failed adoption, the caller retains the descriptor.
    assert(::fcntl(quota_socket.adopted, F_GETFD, 0) >= 0);
    close_if_valid(quota_socket.adopted);
    close_if_valid(quota_socket.peer);

    // Neither AF_UNIX nor random non-socket FDs may be handed to the guest.
    int ux[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, ux) == 0);
    assert(broker.adopt_connected_tcp(*a, ux[0], rights(SocketRight::read)).status ==
           SocketStatus::quota);  // quota rejection takes precedence.
    close_if_valid(ux[0]); close_if_valid(ux[1]);
    auto c = broker.start_tenant(3, {1, 32});
    assert(c);
    int unix_pair[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, unix_pair) == 0);
    assert(broker.adopt_connected_tcp(*c, unix_pair[0], rights(SocketRight::read)).status ==
           SocketStatus::unsupported);
    close_if_valid(unix_pair[0]); close_if_valid(unix_pair[1]);

    assert(broker.close(*a, ax.handle) == SocketStatus::ok);
    assert(::fcntl(ca.adopted, F_GETFD, 0) == -1 && errno == EBADF);
    assert(broker.read(*a, ax.handle, received).status == SocketStatus::denied);
    assert(broker.retire_tenant(*a) == SocketStatus::ok);
    assert(broker.retire_tenant(*a) == SocketStatus::denied);
    auto a2 = broker.start_tenant(1, {1, 8});
    assert(a2);
    auto ca2 = pair_tcp();
    auto ax2 = broker.adopt_connected_tcp(*a2, ca2.adopted,
        rights(SocketRight::read) | rights(SocketRight::close));
    assert(ax2.status == SocketStatus::ok && ax.handle.id != ax2.handle.id);
    assert(broker.read(*a, ax2.handle, received).status == SocketStatus::denied);
    assert(broker.close(*a, ax2.handle) == SocketStatus::denied);

    // Lifecycle race: retire serializes with read; stale ops fail closed.
    std::atomic<bool> stop{false};
    std::thread racer([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            const auto r = broker.read(*a2, ax2.handle, received);
            assert(r.status == SocketStatus::would_block || r.status == SocketStatus::denied ||
                   r.status == SocketStatus::closed);
        }
    });
    assert(broker.retire_tenant(*a2) == SocketStatus::ok);
    stop.store(true, std::memory_order_relaxed);
    racer.join();
    assert(broker.read(*a2, ax2.handle, received).status == SocketStatus::denied);
    assert(::fcntl(ca2.adopted, F_GETFD, 0) == -1 && errno == EBADF);
    assert(broker.close(*b, by.handle) == SocketStatus::ok);
    assert(broker.retire_tenant(*b) == SocketStatus::ok);
    assert(broker.retire_tenant(*c) == SocketStatus::ok);
    close_if_valid(ca.peer); close_if_valid(cb.peer); close_if_valid(ca2.peer);
}
