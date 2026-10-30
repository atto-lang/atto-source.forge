#include "runtime/execution_callback_context.hpp"

#include <atomic>
#include <cassert>
#include <stdexcept>
#include <thread>
#include <type_traits>

using ores::runtime::CallbackBoundary;
using ores::runtime::ExecutionCallbackContext;
using ores::runtime::ExecutionCallbackContextScope;
using ores::runtime::ExecutionContextValue;
using ores::runtime::capture_execution_callback_context;
using ores::runtime::current_execution_callback_context;

int main() {
    static_assert(std::is_trivially_copyable_v<ExecutionCallbackContext>);

    const ExecutionContextValue active{
        .runtime_domain = 17,
        .execution = 41,
        .dispatch = 7,
        .generation = 3,
        .parent_execution = 11,
        .continuation = 5,
    };

    ExecutionCallbackContext outer{};
    assert(capture_execution_callback_context(
        active, CallbackBoundary::host_callback, outer));
    assert(outer.execution == 41);
    assert(current_execution_callback_context() == nullptr);

    ExecutionContextValue nested_active = active;
    nested_active.execution = 42;
    nested_active.dispatch = 8;
    ExecutionCallbackContext inner{};
    assert(capture_execution_callback_context(
        nested_active, CallbackBoundary::timer, inner));

    try {
        ExecutionCallbackContextScope outer_scope(outer);
        const auto* outer_observed = current_execution_callback_context();
        assert(outer_observed != nullptr);
        assert(outer_observed->execution == outer.execution);
        assert(outer_observed->boundary == outer.boundary);
        {
            ExecutionCallbackContextScope inner_scope(inner);
            const auto* inner_observed = current_execution_callback_context();
            assert(inner_observed != nullptr);
            assert(inner_observed->execution == inner.execution);
            assert(inner_observed->boundary == inner.boundary);
        }
        outer_observed = current_execution_callback_context();
        assert(outer_observed != nullptr);
        assert(outer_observed->execution == outer.execution);
        throw std::runtime_error("boom");
    } catch (const std::runtime_error&) {
    }
    assert(current_execution_callback_context() == nullptr);

    std::atomic<bool> foreign_saw_empty{false};
    std::atomic<bool> explicit_scope_ok{false};
    const ExecutionCallbackContext copied = outer;
    std::thread foreign([&] {
        foreign_saw_empty.store(
            current_execution_callback_context() == nullptr,
            std::memory_order_relaxed);
        {
            ExecutionCallbackContextScope scope(copied);
            const auto* observed = current_execution_callback_context();
            explicit_scope_ok.store(
                observed != nullptr
                    && observed->runtime_domain == 17
                    && observed->execution == 41
                    && observed->dispatch == 7
                    && observed->boundary == CallbackBoundary::host_callback,
                std::memory_order_relaxed);
        }
        assert(current_execution_callback_context() == nullptr);
    });
    foreign.join();
    assert(foreign_saw_empty.load(std::memory_order_relaxed));
    assert(explicit_scope_ok.load(std::memory_order_relaxed));

    {
        ExecutionCallbackContextScope temporary_scope(
            ExecutionCallbackContext{
                .runtime_domain = 17,
                .execution = 77,
                .dispatch = 12,
                .generation = 4,
                .parent_execution = 41,
                .continuation = 9,
                .boundary = CallbackBoundary::io,
            });
        const auto* observed = current_execution_callback_context();
        assert(observed != nullptr);
        assert(observed->execution == 77);
        assert(observed->boundary == CallbackBoundary::io);
    }
    assert(current_execution_callback_context() == nullptr);

    bool invalid_scope_rejected = false;
    try {
        ExecutionCallbackContextScope invalid_scope(ExecutionCallbackContext{});
    } catch (const std::invalid_argument&) {
        invalid_scope_rejected = true;
    }
    assert(invalid_scope_rejected);
    assert(current_execution_callback_context() == nullptr);

    ExecutionCallbackContext invalid{};
    ExecutionContextValue zero = active;
    zero.execution = 0;
    assert(!capture_execution_callback_context(
        zero, CallbackBoundary::io, invalid));
    assert(!capture_execution_callback_context(
        active, static_cast<CallbackBoundary>(99), invalid));

    return 0;
}
