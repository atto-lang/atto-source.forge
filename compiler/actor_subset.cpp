#include "compiler/actor_subset.hpp"
#include "runtime/actor_stackless.hpp"
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oresforge::actors {
namespace {
bool alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool space(unsigned char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == '\f';
}
struct Token { std::string value; std::size_t offset; };
std::vector<Token> tokenize(std::string_view text) {
    if (text.size() > 65536) throw std::invalid_argument("actor source exceeds 64 KiB");
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (space(c)) { ++i; continue; }
        if (text.substr(i, 2) == "//") {
            i += 2;
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (text.substr(i, 2) == "/*") {
            const auto start = i;
            i += 2;
            unsigned depth = 1;
            while (depth) {
                if (i == text.size())
                    throw std::invalid_argument("unterminated actor comment at byte " +
                                                std::to_string(start));
                if (text.substr(i, 2) == "/*") {
                    if (++depth > 128) throw std::invalid_argument("actor comment nesting limit");
                    i += 2;
                } else if (text.substr(i, 2) == "*/") {
                    --depth; i += 2;
                } else ++i;
            }
            continue;
        }
        const auto start = i;
        if (alpha(c)) {
            ++i;
            while (i < text.size() &&
                   (alpha(static_cast<unsigned char>(text[i])) ||
                    digit(static_cast<unsigned char>(text[i])))) ++i;
        } else if (digit(c)) {
            ++i;
            while (i < text.size() && digit(static_cast<unsigned char>(text[i]))) ++i;
        } else if (text.substr(i, 2) == "==") {
            i += 2;
        } else if (std::string_view("{}();:=.+-").find(static_cast<char>(c))
                   != std::string_view::npos) {
            ++i;
        } else {
            throw std::invalid_argument("unsupported actor syntax at byte " +
                                        std::to_string(i));
        }
        if (out.size() == 8192) throw std::invalid_argument("actor token budget");
        out.push_back({std::string(text.substr(start, i - start)), start});
    }
    out.push_back({"<eof>", text.size()});
    return out;
}
class Parser final {
    std::vector<Token> tokens_;
    std::size_t at_ = 0;
    const Token& current() const { return tokens_.at(at_); }
    [[noreturn]] void reject(const char* why) const {
        throw std::invalid_argument(std::string(why) + " at byte " +
                                    std::to_string(current().offset));
    }
    bool take(std::string_view word) {
        if (current().value != word) return false;
        ++at_;
        return true;
    }
    void need(std::string_view word) {
        if (!take(word)) reject("unsupported actor source form");
    }
    std::string ident() {
        const auto& s = current().value;
        if (s.empty() || !alpha(static_cast<unsigned char>(s.front())))
            reject("expected actor identifier");
        if (s == "any" || s == "unknown" || s == "async" ||
            s == "self" || s == "int" || s == "pub" || s == "fnc" ||
            s == "actor" || s == "state" || s == "define" || s == "as" ||
            s == "end" || s == "return" || s == "if" || s == "then" ||
            s == "else" || s == "fi" || s == "on_message")
            reject("reserved or unsupported actor identifier");
        auto name = s;
        ++at_;
        return name;
    }
    std::int64_t integer() {
        std::string text;
        if (take("-")) text = "-";
        const auto& s = current().value;
        if (s.empty() || !digit(static_cast<unsigned char>(s.front())))
            reject("expected actor i64 literal");
        text += s;
        ++at_;
        std::int64_t result = 0;
        const auto* first = text.data();
        const auto* last = first + text.size();
        const auto parsed = std::from_chars(first, last, result);
        if (parsed.ec != std::errc{} || parsed.ptr != last)
            reject("actor i64 literal overflow");
        return result;
    }
    std::size_t actor_binding(const std::string& name,
                              const Program& program) const {
        for (std::size_t i = 0; i < program.spawn_bindings.size(); ++i)
            if (name == program.spawn_bindings[i]) return i;
        reject("actor name is not a live spawn binding");
    }
    Program::Invoke invoke(const Program& program) {
        const auto binding = ident();
        need("."); need("on_message"); need("(");
        const auto value = integer(); need(")");
        return {actor_binding(binding, program), value};
    }
    void parse_main(Program& program) {
        need("fnc"); need("main"); need("("); need(")");
        need(":"); need("int"); need("{");
        program.has_main = true;
        while (take("const")) {
            if (program.spawn_bindings.size() == 8)
                reject("source actor spawn quota exceeded");
            const auto binding = ident();
            if (binding == program.actor_name) reject("spawn binding shadows actor type");
            for (const auto& previous : program.spawn_bindings)
                if (previous == binding) reject("duplicate actor binding");
            need("="); need("spawn");
            same(ident(), program.actor_name);
            need("("); need(")"); need(";");
            program.spawn_bindings.push_back(binding);
        }
        if (program.spawn_bindings.size() < 2)
            reject("source actor main requires at least two distinct spawns");
        while (current().value != "return") {
            if (current().value == "}" || current().value == "<eof>")
                reject("source actor main requires return");
            if (program.sends.size() == 256)
                reject("source actor send quota exceeded");
            program.sends.push_back(invoke(program));
            need(";");
        }
        need("return");
        const auto left = invoke(program);
        need("+");
        const auto right = invoke(program);
        need(";");
        need("}");
        program.result = std::make_pair(left, right);
    }
    static void same(const std::string& a, const std::string& b) {
        if (a != b) throw std::invalid_argument("actor type/name mismatch");
    }
public:
    explicit Parser(std::string_view source) : tokens_(tokenize(source)) {}
    Program parse() {
        Program out;
        need("define"); need("actor"); out.actor_name = ident(); need("as");
        if (take("state")) {
            out.handler = Program::Handler::CheckedFieldAdd;
            need("{"); out.field_name = ident(); need(":"); need("int");
            need("="); out.initial = integer(); need(";"); need("}");
        } else {
            out.handler = Program::Handler::RecursiveCountdown;
        }
        need("pub"); need("fnc"); need("on_message"); need("(");
        need("int"); out.parameter_name = ident(); need(")");
        need(":"); need("int"); need("{");
        if (out.handler == Program::Handler::CheckedFieldAdd) {
            need("self"); need("."); same(ident(), out.field_name);
            need("="); need("self"); need("."); same(ident(), out.field_name);
            need("+"); same(ident(), out.parameter_name); need(";");
            need("return"); need("self"); need(".");
            same(ident(), out.field_name); need(";"); need("}");
        } else {
            need("return"); out.recursive_method = ident(); need("(");
            same(ident(), out.parameter_name); need(")"); need(";"); need("}");
            if (out.recursive_method == "on_message" || out.recursive_method == "main")
                reject("invalid recursive method");
            need("pub"); need("fnc");
            same(ident(), out.recursive_method);
            need("("); need("int"); const auto argument = ident();
            need(")"); need(":"); need("int"); need("{");
            need("if"); same(ident(), argument); need("=="); need("0");
            need("then"); need("return"); need("0"); need(";");
            need("else"); need("return"); same(ident(), out.recursive_method);
            need("("); same(ident(), argument); need("-"); need("1");
            need(")"); need(";"); need("fi"); need("}");
        }
        need("end");
        if (take("pub")) parse_main(out);
        if (current().value != "<eof>") reject("trailing unsupported actor source");
        return out;
    }
};
using VM = runtime::ActorStacklessI64;
constexpr std::array<VM::Instruction, 6> countdown{{
    {VM::Op::BranchIfZero, 5},
    {VM::Op::DecrementArg},
    {VM::Op::Call, 0},
    {VM::Op::IncrementChild},
    {VM::Op::ReturnChild},
    {VM::Op::ReturnLiteral, 0, 0},
}};
std::int64_t checked_add(std::int64_t a, std::int64_t b) {
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b))
        throw std::overflow_error("compiled actor i64 state overflow");
    return a + b;
}
}
Program compile(std::string_view source) { return Parser(source).parse(); }
std::string dump_ir(const Program& p) {
    std::string ir;
    if (p.handler == Program::Handler::CheckedFieldAdd)
        ir = "actor " + p.actor_name + " field " + p.field_name +
            " : i64 initial=" + std::to_string(p.initial) +
            "\nhandler on_message(" + p.parameter_name + ") : checked-add-field\n";
    else
        ir = "actor " + p.actor_name + " field none\nhandler on_message(" +
            p.parameter_name + ") : call " + p.recursive_method +
            "\nmethod " + p.recursive_method +
            " : stackless-loop-v1 frames=64 fuel=16384\n";
    if (!p.has_main) return ir;
    ir += "main : synchronous-source-actor-v0\n";
    for (const auto& name : p.spawn_bindings)
        ir += "  spawn " + p.actor_name + " as " + name + "\n";
    for (const auto& call : p.sends)
        ir += "  send " + p.spawn_bindings.at(call.actor_index) + " " +
              std::to_string(call.message) + "\n";
    if (!p.result) throw std::logic_error("compiled actor main missing result");
    ir += "  return checked_add(two actor replies)\n";
    return ir;
}


std::int64_t execute_main(const Program& p) {
    if (!p.has_main || !p.result || p.spawn_bindings.size() < 2 ||
        p.spawn_bindings.size() > 8 || p.sends.size() > 256)
        throw std::invalid_argument("invalid compiled actor main program");
    // No other actor type or dynamic expression is admitted. Every actor
    // incarnation has a fresh nominal capability and native scalar heap.
    Program handler = p;
    handler.has_main = false;
    handler.spawn_bindings.clear();
    handler.sends.clear();
    handler.result.reset();
    std::vector<std::unique_ptr<ActorExecutionContext>> actors;
    actors.reserve(p.spawn_bindings.size());
    for (std::size_t i = 0; i < p.spawn_bindings.size(); ++i)
        actors.push_back(std::make_unique<ActorExecutionContext>(
            handler, static_cast<std::uint64_t>(i + 1)));
    auto send = [&](const Program::Invoke& call) -> std::int64_t {
        if (call.actor_index >= actors.size())
            throw std::invalid_argument("actor invocation index not in spawn table");
        return actors[call.actor_index]->receive(call.message);
    };
    for (const auto& call : p.sends) (void)send(call);
    // Both replies must execute; an actor fault is terminal for the
    // source program and produces no partially published scalar result.
    const auto a = send(p.result->first);
    const auto b = send(p.result->second);
    const auto total = checked_add(a, b);
    for (auto& actor : actors) {
        actor->stop();
        if (!actor->done()) throw std::logic_error("actor did not quiesce");
    }
    return total;
}

ActorExecutionContext::BudgetReservation::BudgetReservation() {
    runtime::ActorMemoryAggregateBudgetV1::process().reserve(bytes);
}
ActorExecutionContext::BudgetReservation::~BudgetReservation() noexcept {
    runtime::ActorMemoryAggregateBudgetV1::process().refund(bytes);
}
thread_local ActorExecutionContext* ActorExecutionContext::active_ = nullptr;
ActorExecutionContext::Activation::Activation(ActorExecutionContext& actor)
    : prior(active_) {
    if (prior) throw std::logic_error("nested actor runtime dispatch denied");
    active_ = &actor;
}
ActorExecutionContext::Activation::~Activation() { active_ = prior; }

ActorExecutionContext::ActorExecutionContext(Program compiled, std::uint64_t actor_id)
    : mailbox_(actor_id, 8, 8 * sizeof(std::int64_t)),
      incarnation_(mailbox_.ref()), program_(std::move(compiled)), budget_(),
      actor_(actor_id, 1, 8, initialize, handle) {
    if (program_.actor_name.empty() || program_.parameter_name.empty())
        throw std::invalid_argument("invalid compiled actor program");
}
void ActorExecutionContext::initialize(OresActorI64Heap* heap,
                                        std::uint64_t domain, std::uint64_t epoch) {
    if (!active_) throw std::logic_error("actor initialization outside lease");
    if (active_->program_.handler == Program::Handler::CheckedFieldAdd)
        ores_actor_i64_heap_field_init_mutable(heap, domain, epoch, 0,
                                               active_->program_.initial);
}
std::int64_t ActorExecutionContext::handle(OresActorI64Heap* heap,
                                           std::uint64_t domain,
                                           std::uint64_t epoch,
                                           std::int64_t message) {
    if (!active_) throw std::logic_error("actor handler outside lease");
    if (active_->program_.handler == Program::Handler::RecursiveCountdown)
        return VM::run(active_->actor_.frame_stack(), countdown, message);
    const auto old = ores_actor_i64_heap_field_read(heap, domain, epoch, 0);
    const auto updated = checked_add(old, message);
    ores_actor_i64_heap_field_write(heap, domain, epoch, 0, updated);
    return ores_actor_i64_heap_field_read(heap, domain, epoch, 0);
}
void ActorExecutionContext::claim_mode(TransportMode desired) {
    auto unset = TransportMode::Unset;
    if (!mode_.compare_exchange_strong(unset, desired) && unset != desired)
        throw std::logic_error("mixed direct and queued actor dispatch denied");
}
std::int64_t ActorExecutionContext::receive(std::int64_t message) {
    claim_mode(TransportMode::Direct);
    std::unique_lock<std::mutex> turn(receive_gate_, std::try_to_lock);
    if (!turn.owns_lock())
        throw std::logic_error("overlapping actor receive denied before enqueue");
    if (!mailbox_.authorizes(incarnation_))
        throw std::logic_error("actor incarnation closed");
    Activation lease(*this);
    try {
        if (actor_.try_send(actor_.ref(), message) != runtime::ScalarActor::Send::Accepted)
            throw std::logic_error("actor message not admitted");
        const auto result = actor_.run_one();
        if (result.status != runtime::ScalarActor::Turn::Executed)
            throw std::logic_error("actor did not execute admitted message");
        if (!mailbox_.authorizes(incarnation_))
            throw std::logic_error("actor incarnation stopped during turn");
        return result.value;
    } catch (...) {
        mailbox_.close();   // revoke the published incarnation on terminal fault
        actor_.stop();
        throw;
    }
}
runtime::ActorRef ActorExecutionContext::incarnation_ref() const {
    return incarnation_;
}
void ActorExecutionContext::enqueue(const runtime::ActorRef& destination,
                                    std::int64_t message) {
    claim_mode(TransportMode::Queued);
    // Bit-preserving conversion avoids signed shifts, including INT64_MIN.
    const auto bits = std::bit_cast<std::uint64_t>(message);
    std::array<std::uint8_t, sizeof(bits)> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<std::uint8_t>(bits >> (i * 8));
    mailbox_.send(destination, payload);
}
std::optional<std::int64_t> ActorExecutionContext::dispatch_queued_locked() {
    if (!mailbox_.authorizes(incarnation_))
        throw std::logic_error("actor incarnation closed");
    auto payload = mailbox_.receive();
    if (!payload) return std::nullopt; // no actor callback on an empty mailbox
    if (payload->size() != sizeof(std::int64_t))
        throw std::logic_error("malformed scalar actor message");
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < payload->size(); ++i)
        bits |= std::uint64_t{(*payload)[i]} << (i * 8);
    const auto value = std::bit_cast<std::int64_t>(bits);
    Activation lease(*this);
    try {
        if (actor_.try_send(actor_.ref(), value) != runtime::ScalarActor::Send::Accepted)
            throw std::logic_error("internal actor stage queue denied");
        const auto result = actor_.run_one();
        if (result.status != runtime::ScalarActor::Turn::Executed)
            throw std::logic_error("actor did not execute queued message");
        // A concurrent STOP cannot make the terminal turn publish a reply.
        if (!mailbox_.authorizes(incarnation_))
            throw std::logic_error("actor stopped during queued execution");
        return result.value;
    } catch (...) {
        mailbox_.close(); // drop pending queued payloads; revoke stale refs
        actor_.stop();
        throw;
    }
}
std::optional<std::int64_t> ActorExecutionContext::dispatch_next() {
    claim_mode(TransportMode::Queued);
    std::unique_lock<std::mutex> turn(receive_gate_, std::try_to_lock);
    if (!turn.owns_lock())
        throw std::logic_error("concurrent actor worker dispatch denied");
    return dispatch_queued_locked();
}
void ActorExecutionContext::stop() {
    mailbox_.close(); // revokes/zeroizes pending queued ingress first
    actor_.stop();
}
bool ActorExecutionContext::done() const {
    return actor_.done() && mailbox_.queued() == 0;
}
runtime::ScalarActorRef ActorExecutionContext::ref() const { return actor_.ref(); }
runtime::ScalarActor::Send ActorExecutionContext::try_send(
    const runtime::ScalarActorRef& ref, std::int64_t message) {
    claim_mode(TransportMode::Direct);
    if (!mailbox_.authorizes(incarnation_))
        return runtime::ScalarActor::Send::Closed;
    return actor_.try_send(ref, message);
}
} // namespace oresforge::actors
