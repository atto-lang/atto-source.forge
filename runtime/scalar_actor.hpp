#pragma once
#include "runtime/arena/ActorOwnedI64Heap.h"
#include "runtime/ActorFrameArena.hpp"
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
// Host bridge for the bounded scalar compiler ABI. Each incarnation owns a
// private heap AND an independently bounded logical stack-frame arena.
// External callers must fence all activity before destruction.
class ScalarActor {
public:
    using Initializer = void (*)(OresActorI64Heap*, std::uint64_t, std::uint64_t);
    using Handler = std::int64_t (*)(OresActorI64Heap*, std::uint64_t, std::uint64_t, std::int64_t);
    enum class Send { Accepted, Full, Closed, Foreign };
    enum class Turn { Initialized, Executed, Empty, Busy, Stopped };
    struct Result { Turn status; std::int64_t value = 0; };
    ScalarActor(std::uint64_t id, std::size_t fields, std::size_t mailbox_capacity,
                Initializer, Handler);
    ~ScalarActor() noexcept;
    ScalarActor(const ScalarActor&) = delete;
    ScalarActor& operator=(const ScalarActor&) = delete;
    ScalarActorRef ref() const;
    Send try_send(const ScalarActorRef&, std::int64_t);
    Result run_one();
    void stop();
    bool done() const;
    std::size_t queued() const;
private:
    const std::uint64_t id_;
    std::shared_ptr<const ScalarActorIdentity> identity_;
    std::vector<std::int64_t> ring_;
    OresActorI64Heap* heap_ = nullptr;
    // Actual byte-frame storage, not replacement of a native carrier stack.
    ActorFrameArena frames_;
    std::uint64_t next_execution_ = 0;
    Initializer initializer_;
    Handler handler_;
    mutable std::mutex mutex_;
    std::size_t head_ = 0, count_ = 0;
    bool active_ = false, stopped_ = false, initialized_ = false;
    void wipe_queue() noexcept;
};
} // namespace oresforge::runtime
