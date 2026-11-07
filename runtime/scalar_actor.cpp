#include "runtime/scalar_actor.hpp"
#include <cstdlib>
#include <stdexcept>

namespace oresforge::runtime {
struct ScalarActorIdentity final {};

ScalarActor::ScalarActor(std::uint64_t id, std::size_t fields, std::size_t capacity,
                         Initializer initialize, Handler handler)
    : id_(id), initializer_(initialize), handler_(handler) {
    // Validate before allocating either mailbox storage or an authority.
    if (!id || !fields || fields > 4096 || !capacity || capacity > 4096
            || !initialize || !handler)
        throw std::invalid_argument("invalid bounded scalar actor configuration");
    // Reserve actor-private frame bytes during construction, never in a
    // guest turn. Failure does not publish an actor with half a stack.
    auto frames = std::make_unique<ActorFrameStack>(id, 1);
    identity_ = std::make_shared<ScalarActorIdentity>();
    ring_.resize(capacity);
    heap_ = ores_actor_i64_heap_open(id, 1, fields);
    frames_ = std::move(frames);
    // Initialization is CONTROL work for the first admitted worker turn.
    // Constructing metadata must not execute a child's code on its parent.
}
void ScalarActor::wipe_queue() noexcept {
    volatile std::int64_t* bytes = ring_.data();
    for (std::size_t i = 0; i < ring_.size(); ++i) bytes[i] = 0;
    head_ = count_ = 0;
}
ScalarActor::~ScalarActor() noexcept {
    if (active_) std::abort(); // external scheduler must fence its callbacks
    wipe_queue();
    if (frames_) frames_->retire();
    if (heap_) {
        ores_actor_i64_heap_retire(heap_);
        if (!ores_actor_i64_heap_try_destroy(heap_)) std::abort();
    }
}
ScalarActorRef ScalarActor::ref() const { return ScalarActorRef(identity_); }
ScalarActor::Send ScalarActor::try_send(const ScalarActorRef& ref, std::int64_t value) {
    std::scoped_lock lock(mutex_);
    // Authority precedes mailbox/lifecycle status, including same numeric id.
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
    // No lifecycle/mailbox lock is held while guest ABI code executes.
    // Sends and stop remain responsive; another carrier gets Busy.
    OresActorTurn turn{};
    try {
        turn = ores_actor_i64_heap_enter(heap_, id_, 1);
    } catch (...) {
        frames_->retire();
        std::scoped_lock lock(mutex_);
        active_ = false;
        stopped_ = true;
        wipe_queue();
        throw;
    }
    std::int64_t value = 0;
    try {
        // Frame authority is active only while the matching heap turn runs.
        // This is separate *logical* frame storage, not a switched OS stack.
        ActorFrameStack::Turn frame_turn(*frames_);
        if (!initialized_) initializer_(heap_, turn.domain, turn.epoch);
        // The synchronized publication below is the only writer of the
        // initialized state. Other run_one calls observe active_ meanwhile.
        if (has_message) value = handler_(heap_, turn.domain, turn.epoch, message);
        // Even callbacks that catch their own frame-limit errors cannot
        // commit the turn after the actor's frame arena has been poisoned.
        if (frames_->poisoned()) throw std::logic_error("actor frame poison");
    } catch (...) {
        frames_->retire();
        ores_actor_i64_heap_leave(heap_, turn);
        std::scoped_lock lock(mutex_);
        active_ = false;
        stopped_ = true;
        wipe_queue();
        throw;
    }
    ores_actor_i64_heap_leave(heap_, turn);
    {
        std::scoped_lock lock(mutex_);
        initialized_ = true;
        active_ = false;
        if (stopped_ && !count_) frames_->retire();
    }
    return {has_message ? Turn::Executed : Turn::Initialized, value};
}
void ScalarActor::stop() {
    std::scoped_lock lock(mutex_);
    stopped_ = true;
    if (!active_ && !count_) frames_->retire();
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
