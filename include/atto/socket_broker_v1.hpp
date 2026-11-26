#pragma once

// TRUSTED HOST ONLY: no guest entry point, native FFI, or sandbox admission.
// This component authorizes already-connected TCP sockets. Connection setup,
// DNS pinning, ingress accept, and sealed-guest integration remain fail-closed.

#if !defined(__unix__) && !defined(__APPLE__)
#error "SocketBroker v1 requires a POSIX host"
#endif

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <span>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>

namespace atto::security {

enum class SocketStatus : std::uint8_t {
    ok, denied, quota, invalid, closed, would_block, io_error, unsupported
};
enum class SocketRight : std::uint8_t { read = 1, write = 2, close = 4 };

constexpr unsigned rights(SocketRight right) noexcept {
    return static_cast<unsigned>(right);
}

struct SocketHandle final {
    std::uint64_t id = 0;  // Opaque guest reference, never an OS descriptor.
};
struct SocketOp final {
    SocketStatus status = SocketStatus::invalid;
    std::size_t bytes = 0;
};
struct SocketAdmission final {
    SocketStatus status = SocketStatus::invalid;
    SocketHandle handle{};
};
struct TenantLimits final {
    std::size_t max_sockets = 0;
    std::uint64_t max_io_bytes = 0;  // Total ingress + egress, lifetime.
};

class SocketBroker;
class TenantAuthority final {
    struct Key final {
        std::uint64_t tenant_id;
        std::uint64_t generation;
    };
    std::shared_ptr<const Key> key_;
    explicit TenantAuthority(std::shared_ptr<const Key> key) : key_(std::move(key)) {}
    friend class SocketBroker;
public:
    TenantAuthority() = delete;
    // Never serialize or expose authority to guest. The trusted runtime owns it.
};

class SocketBroker final {
    struct TenantState final {
        std::shared_ptr<const TenantAuthority::Key> key;
        TenantLimits limits;
        std::size_t open_sockets = 0;
        std::uint64_t charged_io = 0;
    };
    struct SocketState final {
        std::shared_ptr<const TenantAuthority::Key> owner;
        int fd;
        dev_t device;
        ino_t inode;
        unsigned granted_rights;
    };
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, TenantState> tenants_;
    std::unordered_map<std::uint64_t, std::uint64_t> generations_;
    std::unordered_map<std::uint64_t, SocketState> sockets_;
    std::uint64_t next_socket_id_ = 1;

    TenantState* authorized(const TenantAuthority& authority) noexcept {
        const auto& key = authority.key_;
        if (!key) return nullptr;
        const auto it = tenants_.find(key->tenant_id);
        if (it == tenants_.end() || it->second.key != key) return nullptr;
        return &it->second;
    }
    static bool valid_connected_tcp(int fd) noexcept {
        if (fd < 0) return false;
        int type = 0;
        socklen_t type_size = sizeof(type);
        if (::getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_size) != 0 ||
            type_size != sizeof(type) || type != SOCK_STREAM) return false;
#if defined(SO_PROTOCOL)
        int protocol = 0;
        socklen_t protocol_size = sizeof(protocol);
        if (::getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, &protocol, &protocol_size) != 0 ||
            protocol_size != sizeof(protocol) || protocol != IPPROTO_TCP) return false;
#else
        // The host reference must not infer TCP merely from SOCK_STREAM.
        return false;
#endif
        sockaddr_storage local{};
        sockaddr_storage peer{};
        socklen_t local_size = sizeof(local), peer_size = sizeof(peer);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &local_size) != 0 ||
            ::getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &peer_size) != 0) return false;
        return (local.ss_family == AF_INET || local.ss_family == AF_INET6) &&
               local.ss_family == peer.ss_family;
    }
    static bool configure_fd(int fd) noexcept {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        const int fdflags = ::fcntl(fd, F_GETFD, 0);
        if (flags < 0 || fdflags < 0 ||
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
            ::fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC) < 0) return false;
#if defined(SO_NOSIGPIPE)
        const int one = 1;
        if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) != 0) return false;
#endif
        return true;
    }
    static void close_fd(int fd) noexcept { if (fd >= 0) (void)::close(fd); }
    static SocketStatus errno_status() noexcept {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return SocketStatus::would_block;
        return SocketStatus::io_error;
    }

    SocketOp io(const TenantAuthority& authority, SocketHandle handle,
                const void* input, void* output, std::size_t size, unsigned required) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        TenantState* tenant = authorized(authority);
        if (!tenant) return {SocketStatus::denied, 0};
        const auto it = sockets_.find(handle.id);
        if (handle.id == 0 || it == sockets_.end() || it->second.owner != tenant->key ||
            (it->second.granted_rights & required) != required)
            return {SocketStatus::denied, 0};
        if (size == 0) return {SocketStatus::ok, 0};
        if (size > 65536 || size > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()))
            return {SocketStatus::quota, 0};
        if (tenant->charged_io > tenant->limits.max_io_bytes ||
            static_cast<std::uint64_t>(size) > tenant->limits.max_io_bytes - tenant->charged_io)
            return {SocketStatus::quota, 0};
        if ((input == nullptr && output == nullptr) || (input && output))
            return {SocketStatus::invalid, 0};
        ssize_t done;
        if (input) {
#if defined(MSG_NOSIGNAL)
            done = ::send(it->second.fd, input, size, MSG_DONTWAIT | MSG_NOSIGNAL);
#else
            done = ::send(it->second.fd, input, size, MSG_DONTWAIT);
#endif
        } else {
            done = ::recv(it->second.fd, output, size, MSG_DONTWAIT);
        }
        if (done < 0) return {errno_status(), 0};
        if (done == 0 && !input) return {SocketStatus::closed, 0};
        tenant->charged_io += static_cast<std::uint64_t>(done);
        return {SocketStatus::ok, static_cast<std::size_t>(done)};
    }

public:
    SocketBroker() = default;
    SocketBroker(const SocketBroker&) = delete;
    SocketBroker& operator=(const SocketBroker&) = delete;
    ~SocketBroker() {
        for (const auto& [id, socket] : sockets_) { (void)id; close_fd(socket.fd); }
    }

    // Host-authenticated tenant_id comes from admission, NEVER guest input.
    std::optional<TenantAuthority> start_tenant(std::uint64_t tenant_id, TenantLimits limits) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tenant_id == 0 || tenants_.contains(tenant_id)) return std::nullopt;
        const auto it = generations_.find(tenant_id);
        if (it != generations_.end() && it->second == UINT64_MAX) return std::nullopt;
        const std::uint64_t generation = it == generations_.end() ? 1 : it->second + 1;
        auto key = std::make_shared<const TenantAuthority::Key>(
            TenantAuthority::Key{tenant_id, generation});
        tenants_.emplace(tenant_id, TenantState{key, limits});
        generations_[tenant_id] = generation;
        return TenantAuthority{std::move(key)};
    }

    // Takes ownership of fd ONLY if successful. No connect, DNS, bind, or listen.
    // Caller is a trusted host endpoint-policy gate, never a guest syscall shim.
    SocketAdmission adopt_connected_tcp(const TenantAuthority& authority, int fd,
                                        unsigned granted_rights) {
        std::lock_guard<std::mutex> lock(mutex_);
        TenantState* tenant = authorized(authority);
        if (!tenant) return {SocketStatus::denied, {}};
        constexpr unsigned allowed = rights(SocketRight::read) | rights(SocketRight::write) |
                                     rights(SocketRight::close);
        if (granted_rights == 0 || (granted_rights & ~allowed) != 0)
            return {SocketStatus::denied, {}};
        if (tenant->open_sockets >= tenant->limits.max_sockets)
            return {SocketStatus::quota, {}};
        if (!valid_connected_tcp(fd)) return {SocketStatus::unsupported, {}};
        // Bind admission to the underlying socket object, not just its fd.
        // Refuse platforms that cannot provide a usable socket identity.
        struct stat identity{};
        if (::fstat(fd, &identity) != 0 || !S_ISSOCK(identity.st_mode) ||
            identity.st_ino == 0) return {SocketStatus::unsupported, {}};
        for (const auto& [id, socket] : sockets_) {
            (void)id;
            if (socket.fd == fd || (socket.device == identity.st_dev &&
                                    socket.inode == identity.st_ino))
                return {SocketStatus::denied, {}};
        }
        if (next_socket_id_ == UINT64_MAX || !configure_fd(fd))
            return {SocketStatus::io_error, {}};
        const std::uint64_t id = next_socket_id_++;
        sockets_.emplace(id, SocketState{tenant->key, fd, identity.st_dev,
                                         identity.st_ino, granted_rights});
        ++tenant->open_sockets;
        return {SocketStatus::ok, {id}};
    }

    SocketOp read(const TenantAuthority& authority, SocketHandle handle,
                  std::span<std::byte> buffer) noexcept {
        return io(authority, handle, nullptr, buffer.data(), buffer.size(), rights(SocketRight::read));
    }
    SocketOp write(const TenantAuthority& authority, SocketHandle handle,
                   std::span<const std::byte> data) noexcept {
        return io(authority, handle, data.data(), nullptr, data.size(), rights(SocketRight::write));
    }
    SocketStatus close(const TenantAuthority& authority, SocketHandle handle) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        TenantState* tenant = authorized(authority);
        if (!tenant) return SocketStatus::denied;
        const auto it = sockets_.find(handle.id);
        if (handle.id == 0 || it == sockets_.end() || it->second.owner != tenant->key ||
            (it->second.granted_rights & rights(SocketRight::close)) == 0)
            return SocketStatus::denied;
        close_fd(it->second.fd);
        sockets_.erase(it);
        --tenant->open_sockets;
        return SocketStatus::ok;
    }
    SocketStatus retire_tenant(const TenantAuthority& authority) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        TenantState* tenant = authorized(authority);
        if (!tenant) return SocketStatus::denied;
        for (auto it = sockets_.begin(); it != sockets_.end();) {
            if (it->second.owner == tenant->key) {
                close_fd(it->second.fd);
                it = sockets_.erase(it);
            } else ++it;
        }
        tenants_.erase(authority.key_->tenant_id);
        return SocketStatus::ok;
    }
};
}  // namespace atto::security
