// Runtime checks deliberately survive -DNDEBUG. Native policy test only:
// this does not assert that Forge emits compiled guest probes or executable JIT.
#include "../jit/perf_probes.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <limits>

namespace {
void require(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "PERF_NATIVE_TEST_FAILURE: %s\n", msg);
        std::abort();
    }
}
}

int main() {
    using namespace oresforge::jit::perf;
    static_assert(k_probe_ids.size() == 7 && k_probe_ids[0] == 23 && k_probe_ids[6] == 29,
                  "probe IDs must not drift");
    constexpr std::array<std::uint32_t, 7> expected_ids{{23,24,25,26,27,28,29}};
    for (std::size_t slot=0; slot<expected_ids.size(); ++slot) {
        require(k_probe_ids[slot] == expected_ids[slot], "probe ID mismatch");
        require(probe_slot(expected_ids[slot]) == static_cast<int>(slot), "probe slot mismatch");
    }
    static_assert(probe_slot(23) == 0 && probe_slot(29) == 6);
    static_assert(probe_slot(0) == -1 && probe_slot(30) == -1);
    static_assert(should_sample(42, 7) && !should_sample(43, 7));
    static_assert(!should_sample(0, 0) && !should_sample(-1, 1));
    static_assert(!should_sample(0, 65'537));
    static_assert(saturating_increment(0) == 1);
    static_assert(saturating_increment(std::numeric_limits<std::uint64_t>::max()-1)
                    == std::numeric_limits<std::uint64_t>::max());
    static_assert(saturating_increment(std::numeric_limits<std::uint64_t>::max())
                    == std::numeric_limits<std::uint64_t>::max());

    ProbeCounters bank{false};
    require(bank.record(23, 42, 1) == Outcome::disabled, "disabled state");
    require(bank.count(23) == 0, "disabled must not mutate");
    bank.set_enabled(true);
    require(bank.record(23, 42, 0) == Outcome::unsampled, "zero divisor");
    require(bank.record(23, 42, -1) == Outcome::unsampled, "negative divisor");
    require(bank.record(23, 42, 65'537) == Outcome::unsampled, "large divisor");
    require(bank.record(30, 42, 1) == Outcome::unknown_probe, "unknown ID");
    require(bank.record(23, 42, 7) == Outcome::recorded, "recorded");
    require(bank.record(23, 43, 7) == Outcome::unsampled, "unsampled");
    require(bank.count(23) == 1, "exactly once");
    require(bank.count(29) == 0, "isolated slots");
    require(bank.count(30) == 0, "unknown count");

    // Validate every admitted sampling divisor with high-magnitude keys.
    constexpr std::array<std::int64_t, 9> keys{
      0, 1, 2, 3, 42, 9'007'199'254'740'993LL,
      std::numeric_limits<std::int64_t>::max()-1,
      std::numeric_limits<std::int64_t>::max(),-1
    };
    for (std::int64_t divisor=1; divisor<=65'536; ++divisor) {
       for (const auto key : keys) {
         const bool want=(key>=0 && key % divisor == 0);
         require(should_sample(key,divisor)==want, "sampling matrix");
       }
    }
    require(!should_sample(1, 0), "sample divisor zero");
    require(!should_sample(1, -1), "sample divisor negative");
    require(!should_sample(1, 65'537), "sample divisor > 65k");
    require(!should_sample(-1, 1), "negative sample key");
    std::puts("ATTO_FORGE_PERF_NATIVE_POLICY_PASS");
    return 0;
}
