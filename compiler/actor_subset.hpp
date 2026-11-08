#pragma once
#include "runtime/scalar_actor.hpp"
#include "runtime/ActorIncarnationMemoryV1.hpp"
#include <cstdint>
#include <atomic>
#include <array>
#include <span>
#include <optional>
#include <mutex>
#include <memory>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace oresforge::actors {
// Opt-in, fail-closed compiler slice for scalar actor source. Not a general
// actor frontend or native-code generator. All accepted handlers are lowered
// from inspected source syntax, never selected by filename or actor name.
struct Program final {
    enum class Handler { CheckedFieldAdd, RecursiveCountdown };
    std::string actor_name;
    std::string parameter_name;
    std::string field_name;
    std::string recursive_method;
    std::int64_t initial = 0;
    Handler handler = Handler::CheckedFieldAdd;
    // Source main admission is a deliberately restricted stage-0 profile.
    struct Invoke {
        std::size_t actor_index = 0;
        std::int64_t message = 0;
    };
    std::vector<std::string> spawn_bindings;
    std::vector<Invoke> sends;
    std::optional<std::pair<Invoke, Invoke>> result;
    bool has_main = false;
};
Program compile(std::string_view source);
// Execute a compiled .atto main with explicit, bounded synchronous actor
// sends. This is not asynchronous send, source await, or native AOT codegen.
std::int64_t execute_main(const Program& compiled);
std::string dump_ir(const Program& program);

// Host-driven test entry for one *compiled source* actor. No guest scheduling,
// general source 'spawn', suspension, or physical-stack containment claims.
class ActorExecutionContext final {
public:
    explicit ActorExecutionContext(Program compiled, std::uint64_t actor_id);
    ActorExecutionContext(const ActorExecutionContext&) = delete;
    ActorExecutionContext& operator=(const ActorExecutionContext&) = delete;
    std::int64_t receive(std::int64_t message);
    void stop();
    bool done() const;
    runtime::ScalarActorRef ref() const;
    runtime::ScalarActor::Send try_send(const runtime::ScalarActorRef&, std::int64_t);

    // Checked queued transport. enqueue only stores a message in mainline
    // Mailbox; a worker explicitly calls dispatch_next to execute a turn.
    // These methods never expose actor heap/frame tokens to a caller.
    runtime::ActorRef incarnation_ref() const;
    void enqueue(const runtime::ActorRef& destination, std::int64_t message);
    // Owns one claimed mailbox message and its exclusive worker slot.
    // It can be moved but not copied; abandoning an uncommitted ticket
    // terminates this incarnation rather than silently losing accepted data.
    // The context must outlive any outstanding ticket.
    class DispatchTicket final {
        friend class ActorExecutionContext;
        ActorExecutionContext* owner_ = nullptr;
        runtime::ActorRef incarnation_;
        std::int64_t message_ = 0;
        bool committed_ = false;
        DispatchTicket(ActorExecutionContext& owner,
                       const runtime::ActorRef& incarnation,
                       std::int64_t message)
            : owner_(&owner), incarnation_(incarnation), message_(message) {}
    public:
        DispatchTicket(const DispatchTicket&) = delete;
        DispatchTicket& operator=(const DispatchTicket&) = delete;
        DispatchTicket(DispatchTicket&& other) noexcept
            : owner_(std::exchange(other.owner_, nullptr)),
              incarnation_(std::move(other.incarnation_)),
              message_(other.message_), committed_(other.committed_) {}
        DispatchTicket& operator=(DispatchTicket&&) = delete;
        ~DispatchTicket() noexcept;
    };
    std::optional<DispatchTicket> take_next();
    std::int64_t execute_ticket(DispatchTicket&& ticket);
    std::optional<std::int64_t> dispatch_next();
private:
    // Reserve the maximum per-incarnation footprint before constructing
    // the host scalar actor. Refund only after actor destruction/quiescence.
    struct BudgetReservation final {
        static constexpr std::size_t bytes = 128 * 1024;
        BudgetReservation();
        ~BudgetReservation() noexcept;
        BudgetReservation(const BudgetReservation&) = delete;
        BudgetReservation& operator=(const BudgetReservation&) = delete;
    };
    // The mailbox owns generation authority and bounded queued ingress.
    // ScalarActor owns per-turn native heap/frames and stages one dequeued
    // message at a time. This is NOT yet a single heap/frame ABI.
    runtime::Mailbox mailbox_;
    const runtime::ActorRef incarnation_;
    Program program_;
    BudgetReservation budget_;
    runtime::ScalarActor actor_;
    enum class TransportMode : unsigned char { Unset, Direct, Queued };
    std::atomic<TransportMode> mode_{TransportMode::Unset};
    // Direct source calls serialize separately from queued worker claims.
    std::mutex receive_gate_;
    std::atomic<bool> worker_claimed_{false};
    // Serializes STOP closure against successful reply publication. The
    // handler runs outside this lock; STOP never waits for guest execution.
    std::mutex publication_gate_;
    void claim_mode(TransportMode desired);
    void close_incarnation() noexcept;
    static thread_local ActorExecutionContext* active_;
    struct Activation final {
        ActorExecutionContext* prior;
        explicit Activation(ActorExecutionContext& actor);
        ~Activation();
        Activation(const Activation&) = delete;
        Activation& operator=(const Activation&) = delete;
    };
    static void initialize(OresActorI64Heap*, std::uint64_t, std::uint64_t);
    static std::int64_t handle(OresActorI64Heap*, std::uint64_t,
                               std::uint64_t, std::int64_t);
};
} // namespace oresforge::actors
