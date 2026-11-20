#pragma once

// Forge UHL-1 trusted-host discovery: no live guest hot-load authority.
// This header is not yet wired into OresForge actor/compiler dispatch.
// A native/AOT scalar executable is not a supervised actor upgrade.
#include <cstddef>
#include <cstdint>

namespace ores::hotload {
inline constexpr std::uint32_t abi_version = 1;

enum class status : std::uint32_t {
    ok = 0, unsupported = 1, denied = 2, bad_abi = 3,
    stale_epoch = 4, busy = 5, resource_limit = 6,
    migration_failed = 7, quarantined = 8, bad_handle = 9,
    internal = 10
};

struct capabilities {
    std::uint32_t version;
    std::uint64_t feature_bits;
    std::uint64_t max_manifest_bytes;
    std::uint64_t max_checkpoint_bytes;
    std::uint64_t max_live_generations;
};

// No in-process code replacement, supervised subtree/ETS transfer,
// cross-domain migration, ORC-JIT, or AOT-image transition is supported.
[[nodiscard]] constexpr status query_capabilities(
    std::uint32_t requested_version, capabilities* output) noexcept {
    if (output == nullptr) return status::bad_handle;
    *output = {};  // Clear previously supplied privileges on every query.
    if (requested_version != abi_version) return status::bad_abi;
    output->version = abi_version;
    return status::ok;
}

// Never dereference, hash, pin, parse, or run untrusted candidate pointers.
[[nodiscard]] constexpr status stage(const void*, std::size_t) noexcept {
    return status::unsupported;
}
[[nodiscard]] constexpr status prepare(std::uint64_t) noexcept {
    return status::unsupported;
}
[[nodiscard]] constexpr status freeze(std::uint64_t, std::uint64_t) noexcept {
    return status::unsupported;
}
[[nodiscard]] constexpr status quiesce(std::uint64_t) noexcept {
    return status::unsupported;
}
[[nodiscard]] constexpr status commit(std::uint64_t, std::uint64_t) noexcept {
    return status::unsupported;
}
[[nodiscard]] constexpr status abort(std::uint64_t) noexcept {
    return status::unsupported;
}
[[nodiscard]] constexpr status retire(std::uint64_t) noexcept {
    return status::unsupported;
}
[[nodiscard]] constexpr status acknowledge_ets_heir(
    std::uint64_t, std::uint64_t, std::uint64_t) noexcept {
    return status::unsupported;
}
}  // namespace ores::hotload
