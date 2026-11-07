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
                    if (b < 32 || b > 126) fail("non-ASCII source byte");
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
        } else if (std::string_view(":(),;=+-").find(static_cast<char>(c)) == std::string_view::npos) {
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
        if (name == "end" || name == "receive" || name == "return" ||
            name == "define" || name == "as" || name == "actor" ||
            name == "int" || name == "let" || name == "suspends")
            fail("reserved actor identifier");
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
    struct Plan {std::int64_t initial; bool subtract;};
    Plan parse() {
        expect("define"); expect("actor");
        const auto actor = ident(); (void)actor;
        expect("as"); expect("let"); expect("int");
        const auto field = ident();
        expect(":");
        const auto initial = literal();
        expect("receive"); expect("("); expect("int");
        const auto message = ident();
        if (message == field) fail("field and message names collide");
        expect(")"); expect("->"); expect("int"); expect("::");
        expect(field); expect("="); expect(field);
        const bool subtract = token_ == "-";
        if (!subtract) expect("+"); else advance();
        expect(message);
        if (token_ == ";") advance();
        expect("return"); expect(field);
        if (token_ == ";") advance();
        expect("end"); expect("end");
        if (!token_.empty()) fail("trailing actor source");
        return {initial, subtract};
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
    output << plan.initial;
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
              "    const auto before = ores_actor_i64_heap_field_read(h, d, e, 0);\n"
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
