#pragma once
#include "runtime/arena/ActorOwnedI64Heap.h"
#include "runtime/actor_frames.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace oresforge::runtime {
struct ScalarActorIdentity;
class ScalarActor;
class ScalarActorRef {
    std::shared_ptr<const ScalarActorIdentity> identity_;
    explicit ScalarActorRef(std::shared_ptr<const ScalarActorIdentity> identity)
        : identity_(std::move(identity)) {}
    friend class ScalarActor;
};

// Host runtime for the fixed-i64 compiler actor ABI. The caller schedules
// run_one on its own pool; this class creates no threads or interpreter.
// Every instance owns a separate bounded heap and preallocated FIFO.
// Callbacks must not leak loans/heap pointers or outlive this object.
// As with Mailbox, destruction requires all external callers to be fenced.
class ScalarActor {
public:
    using Initializer = void (*)(OresActorI64Heap*, std::uint64_t, std::uint64_t);
    using Handler = std::int64_t (*)(OresActorI64Heap*, std::uint64_t, std::uint64_t, std::int64_t);
    // Compiler-generated stage-0 actor callbacks receive the already-admitted
    // logical frame arena explicitly. This is a trusted native host ABI, never
    // a guest-visible raw pointer or transferable actor authority.
    using FramedInitializer = void (*)(OresActorI64Heap*, std::uint64_t,
                                      std::uint64_t, ActorFrameStack&);
    using FramedHandler = std::int64_t (*)(OresActorI64Heap*, std::uint64_t,
                                          std::uint64_t, std::int64_t, ActorFrameStack&);
    enum class Send { Accepted, Full, Closed, Foreign };
    enum class Turn { Initialized, Executed, Empty, Busy, Stopped };
    struct Result { Turn status; std::int64_t value = 0; };

    ScalarActor(std::uint64_t id, std::size_t fields, std::size_t mailbox_capacity,
                Initializer, Handler);
    ScalarActor(std::uint64_t id, std::size_t fields, std::size_t mailbox_capacity,
                FramedInitializer, FramedHandler);
    ~ScalarActor() noexcept;
    ScalarActor(const ScalarActor&) = delete;
    ScalarActor& operator=(const ScalarActor&) = delete;
    ScalarActorRef ref() const;
    Send try_send(const ScalarActorRef&, std::int64_t);
    Result run_one();
    // Trusted host bridge only: frame data access still requires an admitted
    // same-carrier turn token. No guest raw stack pointer is exposed.
    ActorFrameStack& frame_stack() noexcept { return *frames_; }
    // Close ingress, then allow run_one to drain previously accepted data.
    void stop();
    bool done() const;
    std::size_t queued() const;
private:
    ScalarActor(std::uint64_t id, std::size_t fields, std::size_t mailbox_capacity,
                Initializer, Handler, FramedInitializer, FramedHandler);
    const std::uint64_t id_;
    std::shared_ptr<const ScalarActorIdentity> identity_;
    std::vector<std::int64_t> ring_;
    OresActorI64Heap* heap_ = nullptr;
    std::unique_ptr<ActorFrameStack> frames_;
    Initializer initializer_;
    Handler handler_;
    FramedInitializer framed_initializer_ = nullptr;
    FramedHandler framed_handler_ = nullptr;
    mutable std::mutex mutex_;
    std::size_t head_ = 0, count_ = 0;
    bool active_ = false, stopped_ = false, initialized_ = false;
    void wipe_queue() noexcept;
};
} // namespace oresforge::runtime
