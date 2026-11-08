#pragma once
#include "runtime/scalar_actor.hpp"
#include <cstdint>
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
};
Program compile(std::string_view source);
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
private:
    Program program_;
    runtime::ScalarActor actor_;
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
