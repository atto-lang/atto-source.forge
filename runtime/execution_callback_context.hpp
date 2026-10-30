#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace ores::runtime {

inline constexpr std::size_t max_callback_trace_boundaries = 8;

enum class CallbackBoundary : std::uint8_t {
    host_callback = 1,
    io = 2,
    timer = 3,
    await = 4,
    actor_message = 5,
    supervisor_control = 6,
    task_join = 7,
    spawn = 8,
};

struct ExecutionContextValue final {
    std::uint64_t runtime_domain{};
    std::uint64_t execution{};
    std::uint64_t dispatch{};
    std::uint64_t generation{};
    std::uint64_t parent_execution{};
    std::uint64_t continuation{};
};

struct ExecutionCallbackContext final {
    std::uint64_t runtime_domain{};
    std::uint64_t execution{};
    std::uint64_t dispatch{};
    std::uint64_t generation{};
    std::uint64_t parent_execution{};
    std::uint64_t continuation{};
    CallbackBoundary boundary{CallbackBoundary::host_callback};
    std::array<CallbackBoundary, max_callback_trace_boundaries> trace_boundaries{};
    std::uint8_t trace_count{};
    std::uint64_t trace_elided{};
};

static_assert(std::is_trivially_copyable_v<ExecutionCallbackContext>);
static_assert(std::is_standard_layout_v<ExecutionCallbackContext>);

[[nodiscard]] constexpr bool valid_boundary(CallbackBoundary boundary) noexcept {
    const auto value = static_cast<std::uint8_t>(boundary);
    return value >= static_cast<std::uint8_t>(CallbackBoundary::host_callback)
        && value <= static_cast<std::uint8_t>(CallbackBoundary::spawn);
}

[[nodiscard]] constexpr bool capture_execution_callback_context(
        const ExecutionContextValue& active,
        CallbackBoundary boundary,
        ExecutionCallbackContext& out) noexcept {
    if (active.runtime_domain == 0 || active.execution == 0
            || active.dispatch == 0 || active.generation == 0
            || !valid_boundary(boundary)) {
        return false;
    }
    out = {};
    out.runtime_domain = active.runtime_domain;
    out.execution = active.execution;
    out.dispatch = active.dispatch;
    out.generation = active.generation;
    out.parent_execution = active.parent_execution;
    out.continuation = active.continuation;
    out.boundary = boundary;
    out.trace_boundaries[0] = boundary;
    out.trace_count = 1;
    return true;
}

[[nodiscard]] constexpr bool derive_execution_callback_context(
        const ExecutionCallbackContext& parent,
        CallbackBoundary boundary,
        ExecutionCallbackContext& out) noexcept {
    if (!valid_boundary(boundary)
            || parent.trace_count == 0
            || parent.trace_count > max_callback_trace_boundaries) {
        return false;
    }

    out = parent;
    out.boundary = boundary;
    if (out.trace_count < max_callback_trace_boundaries) {
        out.trace_boundaries[out.trace_count++] = boundary;
        return true;
    }

    for (std::size_t i = 1; i < max_callback_trace_boundaries; ++i) {
        out.trace_boundaries[i - 1] = out.trace_boundaries[i];
    }
    out.trace_boundaries[max_callback_trace_boundaries - 1] = boundary;
    if (out.trace_elided != UINT64_MAX) ++out.trace_elided;
    return true;
}

[[nodiscard]] constexpr bool valid_execution_callback_context(
        const ExecutionCallbackContext& context) noexcept {
    if (context.runtime_domain == 0
            || context.execution == 0
            || context.dispatch == 0
            || context.generation == 0
            || !valid_boundary(context.boundary)
            || context.trace_count == 0
            || context.trace_count > max_callback_trace_boundaries
            || context.trace_boundaries[context.trace_count - 1] != context.boundary) {
        return false;
    }
    for (std::size_t i = 0; i < context.trace_count; ++i) {
        if (!valid_boundary(context.trace_boundaries[i])) return false;
    }
    return true;
}

/*
 * Provenance-only scope. It never installs an actor turn, allocation/arena
 * handle, scheduler ticket, or guest-entry capability.
 */
inline thread_local const ExecutionCallbackContext* current_callback_context = nullptr;

class ExecutionCallbackContextScope final {
public:
    explicit ExecutionCallbackContextScope(
            const ExecutionCallbackContext& context)
        : current_(context),
          previous_(current_callback_context) {
        if (!valid_execution_callback_context(current_)) {
            throw std::invalid_argument("invalid callback execution provenance");
        }
        current_callback_context = &current_;
    }

    ~ExecutionCallbackContextScope() noexcept {
        current_callback_context = previous_;
    }

    ExecutionCallbackContextScope(const ExecutionCallbackContextScope&) = delete;
    ExecutionCallbackContextScope& operator=(const ExecutionCallbackContextScope&) = delete;
    ExecutionCallbackContextScope(ExecutionCallbackContextScope&&) = delete;
    ExecutionCallbackContextScope& operator=(ExecutionCallbackContextScope&&) = delete;

private:
    ExecutionCallbackContext current_;
    const ExecutionCallbackContext* previous_;
};

[[nodiscard]] inline const ExecutionCallbackContext*
current_execution_callback_context() noexcept {
    return current_callback_context;
}

} // namespace ores::runtime
