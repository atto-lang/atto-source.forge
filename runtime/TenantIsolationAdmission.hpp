#pragma once
#include <stdexcept>
#include <string_view>

namespace oresforge::runtime {

// A mailbox identity/actor arena is not an isolated native address space.
// This stage-0 Forge runtime does not have a supervised tenant process.
// Any hostile tenant requirement MUST be rejected before actor construction.
enum class TenantIsolationMode {
    TrustedInProcess,
    SandboxedProcess
};

inline void require_tenant_isolation(TenantIsolationMode requested, bool untrusted) {
    if (untrusted)
        throw std::logic_error("untrusted tenant requires a verified OS sandbox");
    if (requested != TenantIsolationMode::TrustedInProcess)
        throw std::logic_error("Forge sandboxed tenant process is not implemented");
}

} // namespace oresforge::runtime
