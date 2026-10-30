#pragma once

#include <cstdint>
#include <type_traits>

namespace ores::runtime {

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
    out = {
        active.runtime_domain,
        active.execution,
        active.dispatch,
        active.generation,
        active.parent_execution,
        active.continuation,
        boundary,
    };
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
            const ExecutionCallbackContext& context) noexcept
        : previous_(current_callback_context) {
        current_callback_context = &context;
    }

    ~ExecutionCallbackContextScope() noexcept {
        current_callback_context = previous_;
    }

    ExecutionCallbackContextScope(const ExecutionCallbackContextScope&) = delete;
    ExecutionCallbackContextScope& operator=(const ExecutionCallbackContextScope&) = delete;
    ExecutionCallbackContextScope(ExecutionCallbackContextScope&&) = delete;
    ExecutionCallbackContextScope& operator=(ExecutionCallbackContextScope&&) = delete;

private:
    const ExecutionCallbackContext* previous_;
};

[[nodiscard]] inline const ExecutionCallbackContext*
current_execution_callback_context() noexcept {
    return current_callback_context;
}

} // namespace ores::runtime
