#include "shared/oreslang/runtime/NativeSelectContinuationFence.hpp"
#include <cstdlib>
#include <iostream>
#include <thread>
#include <atomic>
#include <utility>
using oreslang::runtime::NativeSelectContinuationFence;
static void require(bool value, const char* why) {
    if (!value) { std::cerr << why << '\n'; std::abort(); }
}
int main() {
    using Fence = NativeSelectContinuationFence<2>;
    Fence::Ticket pending;
    Fence::Execution running;
    {
        Fence owner;
        auto selected = owner.register_continuation(1);
        pending = owner.register_continuation(1);
        running = owner.try_claim(selected);
        require(running.valid() && pending.active(), "initial pending and physical credit");
    }
    require(!pending.active() && !pending.close(), "owner destruction revokes pending");
    require(running.valid() && running.close() && !running.close(), "physical loan survives owner and retires once");
    NativeSelectContinuationFence<4> churn;
    NativeSelectContinuationFence<4>::Ticket stale;
    for (int round = 0; round < 1000000; ++round) {
        const auto generation = churn.generation();
        auto live = churn.register_continuation(generation);
        auto execution = churn.try_claim(live);
        require(execution.valid() && !churn.advance(generation), "physical credit fences rollover");
        require(!stale.active() && !stale.close(), "stale ticket cannot retire successor");
        require(execution.close() && churn.advance(generation), "retire and rollover");
        require(churn.pending_tickets() == 0 && churn.executing() == 0, "credit conservation");
        stale = live;
    }
    require(churn.close() && churn.retirement_ready(), "churn retirement");
    NativeSelectContinuationFence<2> fence;
    auto a = fence.register_continuation(1);
    auto b = fence.register_continuation(1);
    auto lease = fence.try_claim(a);
    require(lease.valid() && fence.executing() == 1 && fence.pending_tickets() == 1, "claim preserves physical credit");
    require(!fence.advance(1) && !fence.try_claim(a).valid(), "generation and replay fence");
    bool denied = false;
    try { (void)fence.register_continuation(1); } catch (const std::overflow_error&) { denied = true; }
    require(denied, "combined quota");
    require(fence.close() && !b.active() && !fence.retirement_ready(), "close retains executing credit");
    auto moved = std::move(lease);
    require(!lease.valid() && moved.valid() && moved.close() && !moved.close(), "exactly once move retirement");
    require(fence.retirement_ready(), "shutdown complete");
    for (int i = 0; i < 256; ++i) {
        NativeSelectContinuationFence<1> racing;
        auto ticket = racing.register_continuation(1);
        std::atomic<int> wins{0};
        std::thread a([&]{ auto execution = racing.try_claim(ticket); if(execution.valid()) ++wins; });
        std::thread b([&]{ if(ticket.close()) ++wins; });
        a.join(); b.join();
        require(wins == 1 && racing.pending_tickets() == 0 && racing.executing() == 0, "one terminal transition");
        require(racing.advance(1), "advance after retirement");
        auto fresh = racing.register_continuation(2);
        require(!racing.try_claim(ticket).valid() && fresh.active(), "stale ticket cannot claim new generation");
        require(fresh.close(), "fresh cleanup");
    }
    std::cout << "Forge continuation execution lease host checks passed\n";
}
