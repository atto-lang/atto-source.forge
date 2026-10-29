#include "runtime/TenantIsolationAdmission.hpp"
#include <cassert>
#include <stdexcept>

using oresforge::runtime::TenantIsolationMode;
using oresforge::runtime::require_tenant_isolation;

template<typename Fn> bool rejected(Fn fn) {
    try { fn(); } catch (const std::logic_error&) { return true; }
    return false;
}

int main() {
    require_tenant_isolation(TenantIsolationMode::TrustedInProcess, false);
    assert(rejected([] {
        require_tenant_isolation(TenantIsolationMode::SandboxedProcess, false);
    }));
    assert(rejected([] {
        require_tenant_isolation(TenantIsolationMode::TrustedInProcess, true);
    }));
    assert(rejected([] {
        require_tenant_isolation(TenantIsolationMode::SandboxedProcess, true);
    }));
    // Unknown/corrupt enum values cannot become a trusted fallback.
    assert(rejected([] {
        require_tenant_isolation(static_cast<TenantIsolationMode>(127), false);
    }));
}
