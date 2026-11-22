// Forge compiler/runtime AOT-first numeric performance hook substrate.
// A standalone fixed-size counter bank, not an executable JIT and not guest
// instrumentation. Thread confinement is mandatory (one actor/engine owner).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace oresforge::jit::perf {

constexpr std::array<std::uint32_t, 7> k_probe_ids{{23, 24, 25, 26, 27, 28, 29}};

constexpr int probe_slot(std::uint32_t id) noexcept {
    switch (id) {
        case 23: return 0;
        case 24: return 1;
        case 25: return 2;
        case 26: return 3;
        case 27: return 4;
        case 28: return 5;
        case 29: return 6;
        default: return -1;
    }
}

constexpr bool should_sample(std::int64_t key, std::int64_t divisor) noexcept {
    return key >= 0 && divisor >= 1 && divisor <= 65536 && (key % divisor) == 0;
}

enum class Outcome {
    disabled,
    unsampled,
    unknown_probe,
    recorded
};

class ProbeCounters final {
public:
    explicit constexpr ProbeCounters(bool enabled) noexcept : enabled_(enabled) {}

    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }

    // No dynamic allocation, locks, system clocks, logger, threads or I/O.
    Outcome record(std::uint32_t id, std::int64_t sample_key,
                   std::int64_t every_n) noexcept {
        if (!enabled_) return Outcome::disabled;
        if (!should_sample(sample_key, every_n)) return Outcome::unsampled;
        const int slot = probe_slot(id);
        if (slot < 0) return Outcome::unknown_probe;
        auto& count = counters_[static_cast<std::size_t>(slot)];
        if (count < std::numeric_limits<std::uint64_t>::max()) ++count;
        return Outcome::recorded;
    }

    std::uint64_t count(std::uint32_t id) const noexcept {
        const int slot = probe_slot(id);
        return slot < 0 ? 0 : counters_[static_cast<std::size_t>(slot)];
    }

private:
    bool enabled_;
    std::array<std::uint64_t, 7> counters_{{}};
};

} // namespace oresforge::jit::perf
