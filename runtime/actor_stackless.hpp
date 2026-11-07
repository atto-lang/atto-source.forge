#pragma once
#include "runtime/actor_frames.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace oresforge::runtime {
// Trusted compiler-lowering substrate, not yet wired to a source .atto actor.
// An iterative VM: no guest call uses a recursive C++ call. All live guest
// register/PC state is charged to the active actor's private frame arena.
// The bounded carrier-local array contains only opaque handles, never values.
class ActorStacklessI64 final {
public:
    enum class Op : std::uint8_t {
        BranchIfZero, DecrementArg, Call, IncrementChild,
        ReturnChild, ReturnLiteral, ReturnArg, Jump
    };
    struct Instruction {
        Op op;
        std::size_t target = 0;
        std::int64_t literal = 0;
    };
    static std::int64_t run(ActorFrameStack& arena,
                            std::span<const Instruction> code,
                            std::int64_t input,
                            std::size_t instruction_budget = 16384) {
        if (code.empty() || code.size() > 4096 || instruction_budget == 0)
            throw std::invalid_argument("invalid actor VM instruction budget");
        // Reject malformed compiler output before modifying actor state.
        for (const auto& instruction : code) {
            switch (instruction.op) {
                case Op::BranchIfZero:
                case Op::Call:
                case Op::Jump:
                    if (instruction.target >= code.size())
                        throw std::invalid_argument("actor VM branch outside code");
                    break;
                case Op::DecrementArg:
                case Op::IncrementChild:
                case Op::ReturnChild:
                case Op::ReturnLiteral:
                case Op::ReturnArg:
                    break;
                default:
                    throw std::invalid_argument("actor VM invalid opcode");
            }
        }
        const auto token = arena.current_turn();
        std::array<ActorFrameStack::Frame, ActorFrameStack::max_frames> handles{};
        std::size_t depth = 1;
        handles[0] = arena.push(token, sizeof(State));
        store(arena, token, handles[0], {0, input, 0});
        for (std::size_t executed = 0; executed < instruction_budget; ++executed) {
            const auto frame = handles[depth - 1];
            auto state = load(arena, token, frame);
            if (state.pc >= code.size()) {
                poison(arena, token);
            }
            const auto instruction = code[static_cast<std::size_t>(state.pc)];
            switch (instruction.op) {
                case Op::BranchIfZero:
                    state.pc = state.arg == 0 ? instruction.target : state.pc + 1;
                    store(arena, token, frame, state);
                    break;
                case Op::DecrementArg:
                    if (state.arg == std::numeric_limits<std::int64_t>::min())
                        poison(arena, token);
                    --state.arg;
                    ++state.pc;
                    store(arena, token, frame, state);
                    break;
                case Op::Call: {
                    // Charge the new frame before publishing its handle.
                    // Excess depth/bytes poisons this actor, not the carrier.
                    state.pc++;
                    store(arena, token, frame, state);
                    const auto child = arena.push(token, sizeof(State));
                    handles[depth++] = child;
                    store(arena, token, child,
                          {static_cast<std::uint64_t>(instruction.target),
                           state.arg, 0});
                    break;
                }
                case Op::IncrementChild:
                    if (state.child == std::numeric_limits<std::int64_t>::max())
                        poison(arena, token);
                    ++state.child;
                    ++state.pc;
                    store(arena, token, frame, state);
                    break;
                case Op::Jump:
                    state.pc = instruction.target;
                    store(arena, token, frame, state);
                    break;
                case Op::ReturnChild:
                    if (const auto value = return_value(arena, token, handles,
                                                         depth, state.child))
                        return *value;
                    break;
                case Op::ReturnLiteral:
                    if (const auto value = return_value(arena, token, handles,
                                                         depth, instruction.literal))
                        return *value;
                    break;
                case Op::ReturnArg:
                    if (const auto value = return_value(arena, token, handles,
                                                         depth, state.arg))
                        return *value;
                    break;
                default:
                    poison(arena, token);
            }
        }
        // Infinite loops and non-terminating guest execution must consume
        // an explicit bounded budget, not monopolize an OS carrier forever.
        poison(arena, token);
    }
private:
    struct State {
        std::uint64_t pc;
        std::int64_t arg;
        std::int64_t child;
    };
    static_assert(std::is_trivially_copyable_v<State>);
    static_assert(sizeof(State) == 24);
    using Token = ActorFrameStack::Token;
    using Frame = ActorFrameStack::Frame;

    static void store(ActorFrameStack& arena, const Token& token,
                      const Frame& frame, const State& state) {
        std::array<std::uint8_t, sizeof(State)> data{};
        std::memcpy(data.data(), &state, sizeof(state));
        arena.write(token, frame, 0, data);
    }
    static State load(ActorFrameStack& arena, const Token& token,
                      const Frame& frame) {
        std::array<std::uint8_t, sizeof(State)> data{};
        arena.read(token, frame, 0, data);
        State state{};
        std::memcpy(&state, data.data(), sizeof(state));
        return state;
    }
    static std::optional<std::int64_t> return_value(
        ActorFrameStack& arena, const Token& token,
        std::array<Frame, ActorFrameStack::max_frames>& handles,
        std::size_t& depth, std::int64_t value) {
        arena.pop(token, handles[depth - 1]);
        --depth;
        if (depth == 0) return value;
        auto parent = load(arena, token, handles[depth - 1]);
        parent.child = value;
        store(arena, token, handles[depth - 1], parent);
        return std::nullopt;
    }
    [[noreturn]] static void poison(ActorFrameStack& arena,
                                     const Token& token) {
        // Deliberately fail the same checked quota path as generated calls.
        // push() will permanently poison this actor's frame arena.
        (void)arena.push(token, ActorFrameStack::max_bytes + 1);
        throw std::logic_error("unreachable actor VM quota");
    }
};
} // namespace oresforge::runtime
