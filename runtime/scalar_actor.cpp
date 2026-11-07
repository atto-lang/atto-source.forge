#include "runtime/scalar_actor.hpp"
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace oresforge::runtime {
struct ScalarActorIdentity final {};

ScalarActor::ScalarActor(std::uint64_t id, std::size_t fields, std::size_t capacity,
                         Initializer initialize, Handler handler)
    : id_(id), frames_(id, 1), initializer_(initialize), handler_(handler) {
    if (!id || !fields || fields > 4096 || !capacity || capacity > 4096
            || !initialize || !handler)
        throw std::invalid_argument("invalid bounded scalar actor configuration");
    identity_ = std::make_shared<ScalarActorIdentity>();
    ring_.resize(capacity);
    heap_ = ores_actor_i64_heap_open(id, 1, fields);
}
void ScalarActor::wipe_queue() noexcept {
    volatile std::int64_t* bytes = ring_.data();
    for (std::size_t i = 0; i < ring_.size(); ++i) bytes[i] = 0;
    head_ = count_ = 0;
}
ScalarActor::~ScalarActor() noexcept {
    if (active_) std::abort(); // scheduler must fence callbacks
    if (!frames_.retire_quiesced()) std::abort();
    wipe_queue();
    if (heap_) {
        ores_actor_i64_heap_retire(heap_);
        if (!ores_actor_i64_heap_try_destroy(heap_)) std::abort();
    }
}
ScalarActorRef ScalarActor::ref() const { return ScalarActorRef(identity_); }
ScalarActor::Send ScalarActor::try_send(const ScalarActorRef& ref, std::int64_t value) {
    std::scoped_lock lock(mutex_);
    if (ref.identity_ != identity_) return Send::Foreign;
    if (stopped_) return Send::Closed;
    if (count_ == ring_.size()) return Send::Full;
    ring_[(head_ + count_) % ring_.size()] = value;
    ++count_;
    return Send::Accepted;
}
ScalarActor::Result ScalarActor::run_one() {
    std::int64_t message = 0;
    bool has_message = false;
    {
        std::scoped_lock lock(mutex_);
        if (active_) return {Turn::Busy};
        if (!count_ && (stopped_ || initialized_))
            return {stopped_ ? Turn::Stopped : Turn::Empty};
        active_ = true;
        if (count_) {
            has_message = true;
            message = ring_[head_];
            ring_[head_] = 0;
            head_ = (head_ + 1) % ring_.size();
            --count_;
        }
    }
    OresActorTurn heap_turn{};
    bool heap_entered = false;
    std::int64_t value = 0;
    try {
        if (next_execution_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("actor execution identity exhausted");
        heap_turn = ores_actor_i64_heap_enter(heap_, id_, 1);
        heap_entered = true;
        auto frame_turn = frames_.enter(++next_execution_);
        {
            ActorFrameArena::ScopedCurrent scope(frames_, frame_turn);
            if (!initialized_) initializer_(heap_, heap_turn.domain, heap_turn.epoch);
            if (has_message)
                value = handler_(heap_, heap_turn.domain, heap_turn.epoch, message);
        }
        // Unwound frames and swallowed quota/protocol violations are terminal.
        frames_.leave(frame_turn);
    } catch (...) {
        frames_.abort_active_turn();
        if (heap_entered) ores_actor_i64_heap_leave(heap_, heap_turn);
        std::scoped_lock lock(mutex_);
        active_ = false;
        stopped_ = true;
        wipe_queue();
        throw;
    }
    ores_actor_i64_heap_leave(heap_, heap_turn);
    {
        std::scoped_lock lock(mutex_);
        initialized_ = true;
        active_ = false;
    }
    return {has_message ? Turn::Executed : Turn::Initialized, value};
}
void ScalarActor::stop() {
    std::scoped_lock lock(mutex_);
    stopped_ = true;
}
bool ScalarActor::done() const {
    std::scoped_lock lock(mutex_);
    return stopped_ && !active_ && !count_;
}
std::size_t ScalarActor::queued() const {
    std::scoped_lock lock(mutex_);
    return count_;
}
} // namespace oresforge::runtime
