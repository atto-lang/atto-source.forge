#include "runtime/uhl1_capability_gate.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

using ores::hotload::capabilities;
using ores::hotload::status;

static_assert(ores::hotload::abi_version == 1);
static_assert(ores::hotload::stage(nullptr, 0) == status::unsupported);
static_assert(ores::hotload::commit(0, 0) == status::unsupported);

int main() {
    capabilities flags{33, ~std::uint64_t{0}, 1, 2, 3};
    if (ores::hotload::query_capabilities(1, &flags) != status::ok ||
        flags.version != 1 || flags.feature_bits != 0 ||
        flags.max_manifest_bytes != 0 || flags.max_checkpoint_bytes != 0 ||
        flags.max_live_generations != 0) return 1;
    if (ores::hotload::query_capabilities(1, nullptr) != status::bad_handle) return 2;

    for (std::uint32_t wrong : {std::uint32_t{0}, std::uint32_t{2},
                                std::numeric_limits<std::uint32_t>::max()}) {
        flags = {33, ~std::uint64_t{0}, 1, 2, 3};
        if (ores::hotload::query_capabilities(wrong, &flags) != status::bad_abi ||
            flags.version || flags.feature_bits || flags.max_manifest_bytes ||
            flags.max_checkpoint_bytes || flags.max_live_generations) return 3;
    }

    // A deliberately non-dereferenceable address: the host-only ABI must not
    // inspect a candidate, and all operations must stay inert under replay.
    const auto* forged = reinterpret_cast<const void*>(std::uintptr_t{1});
    for (std::uint64_t i = 0; i != 10000; ++i) {
        if (ores::hotload::stage(forged, std::numeric_limits<std::size_t>::max()) != status::unsupported ||
            ores::hotload::prepare(i) != status::unsupported ||
            ores::hotload::freeze(i, i) != status::unsupported ||
            ores::hotload::quiesce(i) != status::unsupported ||
            ores::hotload::commit(i, i) != status::unsupported ||
            ores::hotload::abort(i) != status::unsupported ||
            ores::hotload::retire(i) != status::unsupported ||
            ores::hotload::acknowledge_ets_heir(i, i, i) != status::unsupported)
            return 4;
    }
    if (ores::hotload::query_capabilities(1, &flags) != status::ok ||
        flags.feature_bits != 0) return 5;
    std::cout << "PASS Forge UHL-1 host-only unsupported capability gate (10000 rounds)\n";
}
