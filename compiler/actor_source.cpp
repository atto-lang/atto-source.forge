#include "compiler/actor_source.hpp"
#include <charconv>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oresforge::actor_source {
namespace {
constexpr std::size_t kMaxSourceBytes = 8192;
bool letter(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool digit(char c) noexcept { return c >= '0' && c <= '9'; }
bool white(char c) noexcept { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; }
class Parser final {
    std::string_view source_;
    std::size_t next_byte_ = 0, token_at_ = 0;
    std::string_view token_;
    void advance() {
        while (next_byte_ < source_.size()) {
            const auto c = source_[next_byte_];
            if (white(c)) { ++next_byte_; continue; }
            if (c == '/' && next_byte_ + 1 < source_.size() && source_[next_byte_+1] == '/') {
                next_byte_ += 2;
                while (next_byte_ < source_.size() && source_[next_byte_] != '\n') {
                    const auto b = static_cast<unsigned char>(source_[next_byte_++]);
                    if ((b < 32 && b != 9 && b != 13) || b > 126)
                            fail("non-ASCII source byte");
                }
                continue;
            }
            break;
        }
        token_at_ = next_byte_;
        if (next_byte_ == source_.size()) { token_ = {}; return; }
        const auto c = static_cast<unsigned char>(source_[next_byte_]);
        if (c < 33 || c > 126) fail("non-ASCII source byte");
        ++next_byte_;
        if (letter(static_cast<char>(c))) {
            while (next_byte_ < source_.size() &&
                   (letter(source_[next_byte_]) || digit(source_[next_byte_]))) ++next_byte_;
        } else if (digit(static_cast<char>(c))) {
            while (next_byte_ < source_.size() && digit(source_[next_byte_])) ++next_byte_;
        } else if (c == '-' && next_byte_ < source_.size() && source_[next_byte_] == '>') {
            ++next_byte_;
        } else if (c == ':' && next_byte_ < source_.size() && source_[next_byte_] == ':') {
            ++next_byte_;
        } else if (std::string_view(":(),;=+-{}|.").find(static_cast<char>(c)) == std::string_view::npos) {
            fail("unsupported actor syntax");
        }
        token_ = source_.substr(token_at_, next_byte_ - token_at_);
    }
    [[noreturn]] void fail(const char* reason) const {
        throw std::runtime_error(std::string("actor source: ") + reason + " at byte " + std::to_string(token_at_));
    }
    void expect(std::string_view value) {
        if (token_ != value) fail("unexpected token");
        advance();
    }
    std::string ident() {
        if (token_.empty() || !letter(token_[0])) fail("expected identifier");
        const std::string name(token_);
        advance();
        // Fail closed on language keywords, not merely the words in the
        // stage-0 grammar. A keyword admitted as a field or message could
        // acquire different meaning when this profile grows.
        static constexpr std::string_view reserved[] = {
            "actor", "and", "as", "async", "await", "borrow", "break",
            "case", "catch", "class", "const", "continue", "copy",
            "critical", "define", "defer", "dyn", "else", "end",
            "enum", "false", "fnc", "for", "guard", "if", "import",
            "in", "int", "let", "match", "mut", "null", "or", "pub",
            "receive", "recover", "return", "routine", "rt", "select",
            "self", "share", "state", "state_ro", "state_w", "handle_state",
            "handle_state_ro", "handle_state_w", "struct", "suspends", "take",
            "then", "trait", "true", "type", "void", "while", "xor",
            "panic", "raise", "throw", "trap"
        };
        for (const auto keyword : reserved)
            if (name == keyword) fail("reserved actor identifier");
        return name;
    }
    std::int64_t literal() {
        const bool negative = token_ == "-";
        if (negative) advance();
        if (token_.empty() || !digit(token_[0])) fail("expected scalar initializer");
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(token_.data(), token_.data()+token_.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != token_.data()+token_.size()) fail("invalid scalar initializer");
        const auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (value > limit + (negative ? 1u : 0u)) fail("scalar initializer overflow");
        advance();
        if (negative && value == limit + 1u) return std::numeric_limits<std::int64_t>::min();
        const auto signed_value = static_cast<std::int64_t>(value);
        return negative ? -signed_value : signed_value;
    }
public:
    explicit Parser(std::string_view source) : source_(source) {
        if (source.empty() || source.size() > kMaxSourceBytes)
            throw std::runtime_error("actor source: invalid bounded source length");
        advance();
    }
    enum class Scope { legacy, read, write };
    struct Plan {std::int64_t initial; bool subtract; Scope scope;};
    Plan parse() {
        expect("define"); expect("actor");
        const auto actor = ident(); (void)actor;
        expect("as");
        const bool sealed = token_ == "state";
        if (sealed) { advance(); expect("{"); }
        expect("let"); expect("int");
        const auto field = ident();
        expect(":");
        const auto initial = literal();
        if (sealed) { if (token_ == ";") advance(); expect("}"); }
        expect("receive"); expect("("); expect("int");
        const auto message = ident();
        if (message == field) fail("field and message names collide");
        expect(")"); expect("->"); expect("int"); expect("::");
        if (sealed) {
            expect("return");
            const bool read = token_ == "state_ro";
            if (!read) expect("state_w"); else advance();
            expect("("); expect("|"); const auto binder = ident(); expect("|");
            if (binder == field || binder == message) fail("state binder collides with field or message");
            expect("->");
            bool subtract = false;
            const auto projection = [&] { expect(binder); expect("."); expect(field); };
            if (read) { projection(); }
            else {
                expect("{"); projection(); expect("="); projection();
                subtract = token_ == "-"; if (subtract) advance(); else expect("+");
                expect(message); expect(";"); expect("return"); projection(); expect(";"); expect("}");
            }
            expect(")"); if (token_ == ";") advance();
            expect("end"); expect("end"); if (!token_.empty()) fail("trailing actor source");
            return {initial, subtract, read ? Scope::read : Scope::write};
        }
        expect(field); expect("="); expect(field);
        const bool subtract = token_ == "-";
        if (!subtract) expect("+"); else advance();
        expect(message);
        if (token_ == ";") advance();
        expect("return"); expect(field);
        if (token_ == ";") advance();
        expect("end"); expect("end");
        if (!token_.empty()) fail("trailing actor source");
        return {initial, subtract, Scope::legacy};
    }
};
} // namespace

std::string emit_cxx(std::string_view source) {
    // Verify the entire input before emitting any C++ or executable symbol.
    // Unsupported AttoLang constructs never silently compile as host code.
    const auto plan = Parser(source).parse();
    std::ostringstream output;
    output << "// Forge stage-0 verified .atto scalar actor; NOT general actor support.\n"
              "#include \"runtime/arena/ActorOwnedI64Heap.h\"\n"
              "#include \"runtime/actor_frames.hpp\"\n"
              "#include <array>\n#include <cstdint>\n#include <cstring>\n"
              "#include <limits>\n#include <stdexcept>\n"
              "extern \"C\" void oresforge_actor_init(OresActorI64Heap* h, std::uint64_t d, std::uint64_t e, oresforge::runtime::ActorFrameStack& frames) {\n"
              "  (void)frames;\n"
              "  ores_actor_i64_heap_field_init_mutable(h, d, e, 0, ";
    if (plan.initial == std::numeric_limits<std::int64_t>::min())
        output << "std::numeric_limits<std::int64_t>::min()";
    else output << plan.initial;
    output << ");\n}\n"
              "extern \"C\" std::int64_t oresforge_actor_handle(OresActorI64Heap* h, std::uint64_t d, std::uint64_t e, std::int64_t msg, oresforge::runtime::ActorFrameStack& frames) {\n"
              "  const auto token = frames.current_turn();\n"
              "  const auto frame = frames.push(token, 16);\n"
              "  try {\n"
              "    std::array<std::uint8_t, 8> bytes{};\n"
              "    std::memcpy(bytes.data(), &msg, sizeof(msg));\n"
              "    frames.write(token, frame, 0, bytes);\n"
              "    frames.read(token, frame, 0, bytes);\n"
              "    std::int64_t argument = 0;\n"
              "    std::memcpy(&argument, bytes.data(), sizeof(argument));\n"
              ;
    if (plan.scope != Parser::Scope::legacy) {
        // The scope guard drains the native heap loan on normal and exceptional
        // exit, before the frame is popped and before the actor result escapes.
        output << "    const auto result = [&]() -> std::int64_t {\n"
                  "      const OresActorTurn turn{d, e};\n"
                  "      struct Guard { OresActorI64Heap* heap; OresActorLoan loan;\n"
                  "        ~Guard() noexcept { ores_actor_i64_heap_release(heap, loan); } };\n"
                  "      const Guard guard{h, ";
        output << (plan.scope == Parser::Scope::read ? "ores_actor_i64_heap_field_borrow" : "ores_actor_i64_heap_field_borrow_mut");
        output << "(h, turn, 0)};\n"
                  "      const auto old = ores_actor_i64_heap_loan_read(h, turn, guard.loan);\n";
        if (plan.scope == Parser::Scope::read) {
            output << "      (void)argument; return old;\n";
        } else {
            output << "      const auto max = std::numeric_limits<std::int64_t>::max();\n"
                      "      const auto min = std::numeric_limits<std::int64_t>::min();\n";
            if (plan.subtract) {
                output << "      if ((argument < 0 && old > max + argument) || (argument > 0 && old < min + argument))\n"
                          "        throw std::overflow_error(\"actor-local protected subtract overflow\");\n"
                          "      const auto next = static_cast<std::int64_t>(old - argument);\n";
            } else {
                output << "      if ((argument > 0 && old > max - argument) || (argument < 0 && old < min - argument))\n"
                          "        throw std::overflow_error(\"actor-local protected add overflow\");\n"
                          "      const auto next = static_cast<std::int64_t>(old + argument);\n";
            }
            output << "      ores_actor_i64_heap_loan_write(h, turn, guard.loan, next);\n"
                      "      return next;\n";
        }
        output << "    }();\n"
                  "    frames.pop(token, frame);\n"
                  "    return result;\n"
                  "  } catch (...) {\n"
                  "    if (!frames.poisoned()) frames.pop(token, frame);\n"
                  "    throw;\n"
                  "  }\n}\n";
        return output.str();
    }
    output << "    const auto before = ores_actor_i64_heap_field_read(h, d, e, 0);\n"
              "    std::memcpy(bytes.data(), &before, sizeof(before));\n"
              "    frames.write(token, frame, 8, bytes);\n"
              "    frames.read(token, frame, 8, bytes);\n"
              "    std::int64_t old = 0;\n"
              "    std::memcpy(&old, bytes.data(), sizeof(old));\n"
              "    const auto max = std::numeric_limits<std::int64_t>::max();\n"
              "    const auto min = std::numeric_limits<std::int64_t>::min();\n";
    if (plan.subtract) {
        output << "    if ((argument < 0 && old > max + argument) || (argument > 0 && old < min + argument))\n"
                  "      throw std::overflow_error(\"actor-local i64 subtract overflow\");\n"
                  "    const auto next = static_cast<std::int64_t>(old - argument);\n";
    } else {
        output << "    if ((argument > 0 && old > max - argument) || (argument < 0 && old < min - argument))\n"
                  "      throw std::overflow_error(\"actor-local i64 add overflow\");\n"
                  "    const auto next = static_cast<std::int64_t>(old + argument);\n";
    }
    output << "    ores_actor_i64_heap_field_write(h, d, e, 0, next);\n"
              "    frames.pop(token, frame);\n"
              "    return next;\n"
              "  } catch (...) {\n"
              "    if (!frames.poisoned()) frames.pop(token, frame);\n"
              "    throw;\n"
              "  }\n}\n";
    return output.str();
}
} // namespace oresforge::actor_source
