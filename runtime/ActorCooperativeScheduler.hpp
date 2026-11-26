#pragma once
// Single-thread carrier scheduler over existing capability-checked Mailbox.
// Native trusted-host callbacks only; not a compiled guest actor scheduler.
// No promise of timer preemption, worker migration, sandboxing or RT allocation.
#include "runtime/ActorStackDomain.hpp"
#include "runtime/actor.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace oresforge::runtime {
class ActorCooperativeScheduler final {
 public:
  using Handler = std::function<void(ActorStackDomain&, std::span<const std::uint8_t>,
                                     std::uint64_t&)>;
  explicit ActorCooperativeScheduler(std::size_t actor_limit = 1024)
      : owner_(std::this_thread::get_id()), actor_limit_(actor_limit) {
    if (!actor_limit || actor_limit > 65536)
      throw std::invalid_argument("invalid scheduler actor quota");
  }
  ActorCooperativeScheduler(const ActorCooperativeScheduler&) = delete;
  ActorCooperativeScheduler& operator=(const ActorCooperativeScheduler&) = delete;

  // Actor ids cannot be reused in this scheduler incarnation. A fresh
  // ActorCooperativeScheduler is required, so old ActorRefs cannot alias a
  // newly admitted actor after an old domain was retired.
  ActorRef spawn(std::uint64_t id, std::size_t stack_bytes, std::size_t heap_bytes,
                 Handler handler, std::size_t mailbox_messages = 64,
                 std::size_t mailbox_bytes = 64 * 1024) {
    require_owner();
    require_outside_dispatch();
    if (!id || !handler || live_.size() >= actor_limit_ || ever_used_.count(id))
      throw std::logic_error("actor scheduler admission denied");
    auto entry = std::make_unique<Actor>(id, stack_bytes, heap_bytes,
                                         mailbox_messages, mailbox_bytes,
                                         std::move(handler));
    const auto ref = entry->mailbox.ref();
    // No construction callback runs on the parent's native stack.
    ever_used_.emplace(id, true);
    try {
      live_.emplace(id, std::move(entry));
    } catch (...) {
      ever_used_.erase(id);
      throw;
    }
    return ref;
  }

  void send(const ActorRef& recipient, std::span<const std::uint8_t> message) {
    require_owner();
    auto it = live_.find(recipient.id);
    if (it == live_.end()) throw std::logic_error("stale/foreign actor destination");
    // Mailbox authenticates opaque incarnation authority before limits.
    it->second->mailbox.send(recipient, message);
    ready(*it->second);
  }

  // Processes one ready actor exactly once; one message per turn even if
  // other mail is queued. A suspended task retains stack-local state.
  bool run_one() {
    require_owner();
    require_outside_dispatch();
    while (ready_count_ != 0) {
      Actor& actor = *ready_head_;
      auto it = live_.find(actor.id);
      if (it == live_.end() || it->second.get() != &actor)
        std::terminate(); // An intrusive ready node must remain live.
      purge_ready(actor);
      try {
        if (!actor.started) {
          actor.started = true;
          (void)actor.stack.start_task([&actor](ActorStackDomain& domain) {
            std::uint64_t processed = 0; // lives on the guarded actor stack
            while (!actor.stopping) {
              auto message = actor.mailbox.receive();
              if (message) {
                actor.handler(domain, *message, processed);
                ++processed;
              }
              if (actor.stopping) return;
              // Cooperative task yields after at most one mailbox item.
              domain.yield();
            }
          });
        } else {
          (void)actor.stack.resume_task();
        }
      } catch (const ActorStackDomain::Cancelled&) {
        if (!actor.stopping) {
          actor.mailbox.close();
          actor.stack.stop();
          purge_ready(actor); // A self-send may have queued this retired actor.
          live_.erase(it);
          throw;
        }
      } catch (...) {
        actor.mailbox.close();
        if (actor.stack.task_state() == ActorStackDomain::TaskState::Suspended ||
            actor.stack.task_state() == ActorStackDomain::TaskState::Running)
          std::terminate(); // Cannot release a live native continuation.
        actor.stack.stop();
        purge_ready(actor); // A self-send may have queued this retired actor.
        live_.erase(it);
        throw;
      }
      if (actor.stack.task_state() == ActorStackDomain::TaskState::Finished) {
        actor.mailbox.close();
        actor.stack.stop();
        purge_ready(actor); // A self-send may have queued this retired actor.
        live_.erase(it);
      } else if (actor.stopping || actor.mailbox.queued() != 0) {
        ready(actor);
      }
      return true;
    }
    return false;
  }

  std::size_t run_ready(std::size_t max_turns) {
    require_owner();
    if (max_turns == 0) throw std::invalid_argument("zero-turn scheduler budget");
    std::size_t turns = 0;
    while (turns < max_turns && run_one()) ++turns;
    return turns;
  }

  // STOP revokes ingress and drops pending mailbox-owned bytes immediately.
  // Suspended continuations are resumed once to unwind via Cancelled. This is
  // deliberately distinct from ScalarActor #101's drain-on-stop contract.
  void stop_actor(std::uint64_t id) {
    require_owner();
    auto it = live_.find(id);
    if (it == live_.end()) throw std::logic_error("stale actor stop");
    Actor& actor = *it->second;
    actor.stopping = true;
    actor.mailbox.close();
    if (!actor.started) {
      purge_ready(actor);
      actor.stack.stop();
      live_.erase(it);
    } else if (actor.stack.task_state() == ActorStackDomain::TaskState::Suspended) {
      actor.stack.request_cancel();
      ready(actor);
    }
    // If invoked by the same running actor, its loop returns after handler.
  }

  void stop_all() {
    require_owner();
    require_outside_dispatch();
    std::vector<std::uint64_t> ids;
    ids.reserve(live_.size());
    for (const auto& [id, actor] : live_) ids.push_back(id);
    for (const auto id : ids) stop_actor(id);
    // Cooperative tasks are permitted to catch Cancelled; never spin forever.
    const std::size_t budget = ids.size() * 2 + 1;
    run_ready(budget);
    if (!live_.empty()) throw std::logic_error("actors did not quiesce");
  }

  std::size_t live_actors() const noexcept { return live_.size(); }
  std::size_t queued_turns() const noexcept { return ready_count_; }

 private:
  struct Actor {
    Actor(std::uint64_t id, std::size_t stack_bytes, std::size_t heap_bytes,
          std::size_t max_messages, std::size_t max_bytes, Handler callback)
      : id(id), stack(id,stack_bytes,heap_bytes),
        mailbox(id,max_messages,max_bytes), handler(std::move(callback)) {}
    std::uint64_t id;
    ActorStackDomain stack;
    Mailbox mailbox;
    Handler handler;
    Actor* ready_prev = nullptr;
    Actor* ready_next = nullptr;
    bool queued=false, started=false, stopping=false;
  };
  const std::thread::id owner_;
  const std::size_t actor_limit_;
  std::map<std::uint64_t, std::unique_ptr<Actor>> live_;
  std::map<std::uint64_t, bool> ever_used_;
  // Non-owning links refer exclusively to entries owned by live_.
  Actor* ready_head_ = nullptr;
  Actor* ready_tail_ = nullptr;
  std::size_t ready_count_ = 0;

  void require_owner() const {
    if (std::this_thread::get_id() != owner_)
      throw std::logic_error("scheduler carrier thread migration forbidden");
  }
  static void require_outside_dispatch() {
    if (ActorStackDomain::inside_actor_turn())
      throw std::logic_error("nested actor scheduler dispatch forbidden");
  }
  void ready(Actor& actor) {
    if (actor.queued) return;
    if (ready_count_ >= actor_limit_)
      throw std::logic_error("bounded scheduler ready queue exhausted");
    actor.ready_prev = ready_tail_;
    actor.ready_next = nullptr;
    if (ready_tail_) ready_tail_->ready_next = &actor;
    else ready_head_ = &actor;
    ready_tail_ = &actor;
    ++ready_count_;
    actor.queued = true;
  }
  void purge_ready(Actor& actor) noexcept {
    if (!actor.queued) return;
    // Unlink before any owning live_ erase, including self-send retirement.
    if (actor.ready_prev) actor.ready_prev->ready_next = actor.ready_next;
    else ready_head_ = actor.ready_next;
    if (actor.ready_next) actor.ready_next->ready_prev = actor.ready_prev;
    else ready_tail_ = actor.ready_prev;
    actor.ready_prev = actor.ready_next = nullptr;
    actor.queued = false;
    --ready_count_;
  }
};
} // namespace oresforge::runtime
