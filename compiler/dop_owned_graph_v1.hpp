#pragma once

#include "compiler/dop_owned_f32_v1.hpp"
#include "compiler/execution_graph_v1.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace oresforge::dop_v1 {

// A one-shot host bridge. The F32 slice is intentionally independent of the
// checked I64 graph; no implicit cast, device transfer, or guest AST exists.
class OwnedGraphFuture {
    struct State {
        std::mutex mutex;
        std::atomic<bool> claimed{false};
        std::atomic<bool> cancelled{false};
        bool settled = false;
        std::promise<std::int64_t> promise;
        std::shared_future<std::int64_t> value = promise.get_future().share();

        struct Admission {
            std::mutex mutex;
            std::condition_variable cv;
            bool resolved = false;
            bool accepted = false;
            void resolve(bool ok) noexcept {
                {
                    std::lock_guard<std::mutex> hold(mutex);
                    resolved = true;
                    accepted = ok;
                }
                cv.notify_all();
            }
        };
        bool cancel() {
            std::lock_guard<std::mutex> hold(mutex);
            if (settled) return false;
            settled = true;
            cancelled.store(true, std::memory_order_release);
            promise.set_exception(std::make_exception_ptr(execution_v1::Cancellation()));
            return true;
        }
        void fail(std::exception_ptr reason) {
            std::lock_guard<std::mutex> hold(mutex);
            if (settled) return;
            settled = true;
            promise.set_exception(std::move(reason));
        }
        void complete(std::int64_t result) {
            std::lock_guard<std::mutex> hold(mutex);
            if (settled) return;
            settled = true;
            promise.set_value(result);
        }
        void reject(std::exception_ptr reason) {
            cancelled.store(true, std::memory_order_release);
            fail(std::move(reason));
        }
    };
    std::shared_ptr<State> state_;
    explicit OwnedGraphFuture(std::shared_ptr<State> state) : state_(std::move(state)) {}
public:
    bool cancel() { return state_->cancel(); }
    bool ready() const {
        return state_->value.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready;
    }
    std::int64_t get() const { return state_->value.get(); }

    friend OwnedGraphFuture submit_owned_graph(
        std::shared_ptr<HostBuffer>, std::shared_ptr<HostBuffer>,
        HostBuffer::Area, HostBuffer::Area, float, float,
        execution_v1::Plan, const std::function<void(std::function<void()>)>&);
};

// schedule must be a bounded trusted CPU executor. The gate ensures a broken
// scheduler that starts a callback then throws cannot commit ANY buffer write.
// Inline dispatch is refused to avoid parking the scheduler thread.
inline OwnedGraphFuture submit_owned_graph(
        std::shared_ptr<HostBuffer> input, std::shared_ptr<HostBuffer> output,
        HostBuffer::Area source, HostBuffer::Area destination,
        float scale, float bias, execution_v1::Plan scalar,
        const std::function<void(std::function<void()>)>& schedule) {
    auto state = std::make_shared<OwnedGraphFuture::State>();
    auto admission = std::make_shared<OwnedGraphFuture::State::Admission>();
    auto inline_attempted = std::make_shared<std::atomic<bool>>(false);
    const auto submitting_thread = std::this_thread::get_id();
    OwnedGraphFuture result(state);
    try {
        if (!scalar.cpu_only() ||
                scalar.result_type() != execution_v1::Kind::HostI64)
            throw std::invalid_argument("device/transfer graph or non-I64 result not supported");
        if (!input || !output || !schedule)
            throw std::invalid_argument("missing owned F32 buffer or trusted executor");
        if (input == output || !(input->shape() == output->shape()) ||
                !std::isfinite(scale) || !std::isfinite(bias) ||
                !input->owns_area(source, Mode::Read) ||
                !output->owns_area(destination, Mode::Write) ||
                source.field() != destination.field() ||
                source.begin() != destination.begin() ||
                source.end() != destination.end())
            throw std::invalid_argument("invalid owned F32 graph footprint or coefficients");

        // Capture strong owners. Area is a weak capability and cannot
        // accidentally extend storage authority after a buffer closes.
        schedule([state, admission, inline_attempted, submitting_thread,
                  input = std::move(input), output = std::move(output),
                  source = std::move(source), destination = std::move(destination),
                  scale, bias, scalar = std::move(scalar)]() mutable {
            if (state->claimed.exchange(true, std::memory_order_acq_rel) ||
                    state->cancelled.load(std::memory_order_acquire)) return;
            try {
                {
                    std::unique_lock<std::mutex> lock(admission->mutex);
                    if (!admission->resolved &&
                            std::this_thread::get_id() == submitting_thread) {
                        inline_attempted->store(true, std::memory_order_release);
                        return;
                    }
                    if (!admission->cv.wait_for(lock, std::chrono::seconds(30),
                            [&] { return admission->resolved; })) {
                        state->reject(std::make_exception_ptr(std::runtime_error(
                            "owned CPU graph scheduler admission timed out")));
                        return;
                    }
                    if (!admission->accepted) return;
                }
                if (state->cancelled.load(std::memory_order_acquire)) return;
                execute_cpu_region(*input, *output, source, destination, scale, bias);
                if (state->cancelled.load(std::memory_order_acquire)) return;
                auto value = scalar.execute_cpu();
                if (!state->cancelled.load(std::memory_order_acquire))
                    state->complete(std::get<std::int64_t>(value));
            } catch (...) {
                state->fail(std::current_exception());
            }
        });
        if (inline_attempted->load(std::memory_order_acquire)) {
            state->reject(std::make_exception_ptr(std::logic_error(
                "owned CPU graph requires queued executor, not inline dispatch")));
            admission->resolve(false);
        } else {
            admission->resolve(true);
        }
    } catch (...) {
        state->reject(std::current_exception());
        admission->resolve(false);
    }
    return result;
}

} // namespace oresforge::dop_v1
