// Compile with -std=c++17 -Wall -Wextra -Werror; host policy test only.
#include "../jit/perf_probes.hpp"
#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    using namespace oresforge::jit::perf;
    static_assert(k_probe_ids.size() == 7);
    static_assert(probe_slot(23) == 0 && probe_slot(29) == 6);
    static_assert(probe_slot(0) == -1 && probe_slot(30) == -1);
    static_assert(should_sample(42, 7));
    static_assert(!should_sample(43, 7));
    static_assert(!should_sample(0, 0));
    static_assert(!should_sample(-1, 1));
    static_assert(!should_sample(0, 65537));
    static_assert(should_sample(std::numeric_limits<std::int64_t>::max(), 1));
    ProbeCounters bank{false};
    assert(bank.record(23, 42, 1) == Outcome::disabled);
    assert(bank.count(23) == 0);
    bank.set_enabled(true);
    assert(bank.record(23, 42, 0) == Outcome::unsampled);
    assert(bank.record(30, 42, 1) == Outcome::unknown_probe);
    assert(bank.record(23, 42, 7) == Outcome::recorded);
    assert(bank.count(23) == 1);
    assert(bank.count(29) == 0);
    assert(bank.count(30) == 0);
    for (auto divisor = 1; divisor <= 65536; divisor++) {
        const auto max = std::numeric_limits<std::int64_t>::max();
        assert(should_sample(max, divisor) == (max % divisor == 0));
    }
    return 0;
}
