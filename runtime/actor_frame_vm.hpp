#pragma once
#include "runtime/actor_frames.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oresforge::runtime {

// Host-only foundation for a future compiler-lowered, *stackless* .atto actor
// interpreter. These are sealed typed scalar instructions, not a language
// frontend, native stack switch, FFI, or arbitrary guest callback executor.
class ActorScalarFrameVM final {
public:
    enum class Op : std::uint8_t {
        LoadArgument, LoadConstant, AddArgument, MultiplyArgument,
        SaveTemporary, AddTemporary, BranchArgumentLe, Call, Return
    };
    struct Instruction final {
        Op op;
        std::int64_t immediate = 0;
        std::size_t target = 0; // branch PC or call function index
    };
    struct Function final { std::vector<Instruction> instructions; };
    static constexpr std::size_t max_functions = 64;
    static constexpr std::size_t max_instructions_per_function = 128;
    static constexpr std::size_t instruction_quota = 16384;
    static constexpr std::size_t frame_bytes = 32;

    explicit ActorScalarFrameVM(std::vector<Function> functions)
        : functions_(std::move(functions)) {
        verify();
    }
    ActorScalarFrameVM(const ActorScalarFrameVM&) = delete;
    ActorScalarFrameVM& operator=(const ActorScalarFrameVM&) = delete;

    // An external ScalarActor turn must already admit this carrier. Exception
    // propagation is intentional: ScalarActor retires only the offending
    // actor while keeping sibling heaps and carrier thread operational.
    std::int64_t execute(ActorFrameStack& frames, std::size_t entry,
                         std::int64_t argument) const {
        if (entry >= functions_.size()) throw std::invalid_argument("unknown actor function");
        const auto token = frames.current_turn();
        if (frames.depth(token) != 0) {
            frames.retire();
            throw std::logic_error("scalar frame VM cannot enter a nonempty stack");
        }
        std::array<ActorFrameStack::Frame, ActorFrameStack::max_frames> handles{};
        std::array<std::size_t, ActorFrameStack::max_frames> function_ids{};
        std::size_t depth = 0;
        auto push = [&](std::size_t fn, std::int64_t arg) {
            // ActorFrameStack itself poisons and zeroizes on exhausted quota.
            const auto handle = frames.push(token, frame_bytes);
            State state{};
            state.argument = arg;
            store(frames, token, handle, state);
            handles[depth] = handle;
            function_ids[depth] = fn;
            ++depth;
        };
        push(entry, argument);
        std::size_t consumed = 0;
        while (depth != 0) {
            // Admit exactly instruction_quota instructions, not quota - 1.
            // A terminal return at the inclusive boundary is valid.
            if (consumed == instruction_quota) {
                frames.retire(); // uncatchable actor-local terminal fault
                throw std::length_error("actor scalar VM execution quota exceeded");
            }
            ++consumed;
            auto& frame = handles[depth - 1];
            auto state = load(frames, token, frame);
            const auto& code = functions_[function_ids[depth - 1]].instructions;
            // Verified control flow should never reach outside the function.
            if (state.pc >= code.size())
                throw std::logic_error("actor scalar VM invalid program counter");
            const auto& op = code[static_cast<std::size_t>(state.pc)];
            try {
            switch (op.op) {
            case Op::LoadArgument:
                state.accumulator = state.argument;
                ++state.pc;
                break;
            case Op::LoadConstant:
                state.accumulator = op.immediate;
                ++state.pc;
                break;
            case Op::AddArgument:
                state.accumulator = checked_add(state.accumulator, state.argument);
                ++state.pc;
                break;
            case Op::MultiplyArgument:
                state.accumulator = checked_mul(state.accumulator, state.argument);
                ++state.pc;
                break;
            case Op::SaveTemporary:
                state.temporary = state.accumulator;
                ++state.pc;
                break;
            case Op::AddTemporary:
                state.accumulator = checked_add(state.accumulator, state.temporary);
                ++state.pc;
                break;
            case Op::BranchArgumentLe:
                state.pc = state.argument <= op.immediate ?
                           static_cast<std::uint64_t>(op.target) : state.pc + 1;
                break;
            case Op::Call: {
                const auto child = checked_add(state.argument, op.immediate);
                ++state.pc;
                store(frames, token, frame, state); // commit parent continuation
                push(op.target, child);
                continue;
            }
            case Op::Return: {
                const auto result = state.accumulator;
                frames.pop(token, frame); // zeroizes private frame bytes
                --depth;
                if (!depth) return result;
                auto parent = load(frames, token, handles[depth - 1]);
                parent.accumulator = result;
                store(frames, token, handles[depth - 1], parent);
                continue;
            }
            default:
                throw std::logic_error("actor scalar VM encountered unknown opcode");
            }
            store(frames, token, frame, state);
            } catch (...) {
                // A callback may catch its own exception. Revoking the frame
                // arena makes the enclosing ScalarActor turn fail closed.
                frames.retire();
                throw;
            }
        }
        frames.retire();
        throw std::logic_error("actor scalar VM escaped without return");
    }

private:
    struct State final {
        std::int64_t argument = 0;
        std::int64_t accumulator = 0;
        std::int64_t temporary = 0;
        std::uint64_t pc = 0;
    };
    static_assert(sizeof(State) == frame_bytes);

    static void store(ActorFrameStack& frames, const ActorFrameStack::Token& token,
                      const ActorFrameStack::Frame& frame, const State& state) {
        std::array<std::uint8_t, frame_bytes> bytes{};
        std::memcpy(bytes.data(), &state, bytes.size());
        frames.write(token, frame, 0, bytes);
    }
    static State load(ActorFrameStack& frames, const ActorFrameStack::Token& token,
                      const ActorFrameStack::Frame& frame) {
        std::array<std::uint8_t, frame_bytes> bytes{};
        frames.read(token, frame, 0, bytes);
        State state{};
        std::memcpy(&state, bytes.data(), bytes.size());
        return state;
    }
    static std::int64_t checked_add(std::int64_t a, std::int64_t b) {
        constexpr auto hi = std::numeric_limits<std::int64_t>::max();
        constexpr auto lo = std::numeric_limits<std::int64_t>::min();
        if ((b > 0 && a > hi-b) || (b < 0 && a < lo-b))
            throw std::overflow_error("actor scalar VM signed addition overflow");
        return static_cast<std::int64_t>(a+b);
    }
    static std::int64_t checked_mul(std::int64_t a, std::int64_t b) {
        constexpr auto hi = std::numeric_limits<std::int64_t>::max();
        constexpr auto lo = std::numeric_limits<std::int64_t>::min();
        if (!a || !b) return 0;
        if ((a == -1 && b == lo) || (b == -1 && a == lo))
            throw std::overflow_error("actor scalar VM signed multiplication overflow");
        if ((a > 0 && b > 0 && a > hi/b) ||
            (a > 0 && b < 0 && b < lo/a) ||
            (a < 0 && b > 0 && a < lo/b) ||
            (a < 0 && b < 0 && a < hi/b))
            throw std::overflow_error("actor scalar VM signed multiplication overflow");
        return static_cast<std::int64_t>(a*b);
    }
    void verify() const {
        if (functions_.empty() || functions_.size() > max_functions)
            throw std::invalid_argument("actor scalar VM function budget invalid");
        for (const auto& fn : functions_) {
            const auto& code = fn.instructions;
            if (code.empty() || code.size() > max_instructions_per_function)
                throw std::invalid_argument("actor scalar VM instruction budget invalid");
            if (code.back().op != Op::Return)
                throw std::invalid_argument("actor scalar VM function must end in return");
            bool has_return = false;
            // Validate every instruction, including unreachable instructions.
            // Forward-only branches eliminate same-frame unbounded loops;
            // all recursion is through bounded, explicit logical frames.
            for (std::size_t pc = 0; pc < code.size(); ++pc) {
                const auto& i = code[pc];
                switch (i.op) {
                case Op::LoadArgument:
                case Op::AddArgument:
                case Op::MultiplyArgument:
                case Op::SaveTemporary:
                case Op::AddTemporary:
                    if (i.immediate != 0 || i.target != 0)
                        throw std::invalid_argument("actor scalar VM noncanonical opcode");
                    [[fallthrough]];
                case Op::LoadConstant:
                    if (i.op == Op::LoadConstant && i.target != 0)
                        throw std::invalid_argument("actor scalar VM noncanonical literal");
                    if (pc + 1 >= code.size())
                        throw std::invalid_argument("actor scalar VM fallthrough missing");
                    break;
                case Op::BranchArgumentLe:
                    if (i.target <= pc || i.target >= code.size() ||
                        pc + 1 >= code.size())
                        throw std::invalid_argument("actor scalar VM invalid forward branch");
                    break;
                case Op::Call:
                    if (i.target >= functions_.size() || pc + 1 >= code.size())
                        throw std::invalid_argument("actor scalar VM invalid call");
                    break;
                case Op::Return:
                    if (i.immediate != 0 || i.target != 0)
                        throw std::invalid_argument("actor scalar VM noncanonical return");
                    has_return = true;
                    break;
                default:
                    throw std::invalid_argument("actor scalar VM unknown opcode");
                }
            }
            if (!has_return)
                throw std::invalid_argument("actor scalar VM no return");
        }
    }
    const std::vector<Function> functions_;
};
} // namespace oresforge::runtime
