#pragma once
// Restricted native Atto scalar parser: one main() with one return expression.
#include "atto/scalar_lexer.hpp"
#include "atto/decimal_i64.hpp"
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atto::forge::scalar {
enum class Op { literal, add, sub, mul, div, rem, neg, band, bor, bxor, bnot, shl, lshr, ashr, seq, inline_task };
struct Node {
  Op op;
  std::size_t left = 0, right = 0;
  i64 value = 0;
};
enum class ParseError {
  none, lexical, syntax, literal_out_of_range, depth_limit, node_limit,
  io_direction_unsupported, unsupported_continuous_pipe,
  ownership_bang_unsupported, task_unsupported, task_return_type,
  reserved_sync
};
// Stable sanitized diagnostics; never echo user source bytes.
[[nodiscard]] constexpr std::string_view parse_error_name(ParseError error) noexcept {
  switch (error) {
    case ParseError::none: return "NONE";
    case ParseError::lexical: return "E_PARSE_LEXICAL";
    case ParseError::syntax: return "E_PARSE_SYNTAX";
    case ParseError::literal_out_of_range: return "E_PARSE_RANGE";
    case ParseError::depth_limit: return "E_PARSE_DEPTH";
    case ParseError::node_limit: return "E_PARSE_NODES";
    case ParseError::io_direction_unsupported: return "E_IO_DIRECTION_UNSUPPORTED";
    case ParseError::unsupported_continuous_pipe: return "E_UNSUPPORTED_CONTINUOUS_PIPE";
    case ParseError::ownership_bang_unsupported: return "E_OWNERSHIP_BANG_UNSUPPORTED";
    case ParseError::task_unsupported: return "E_TASK_UNSUPPORTED";
    case ParseError::task_return_type: return "E_TASK_RETURN_TYPE";
    case ParseError::reserved_sync: return "E_RESERVED";
  }
  return "E_PARSE_UNKNOWN";
}

struct Parsed {
  std::vector<Node> nodes;
  std::size_t root = 0;
  ParseError error = ParseError::none;
  std::size_t error_offset = 0;
  [[nodiscard]] bool ok() const noexcept {
    return error == ParseError::none && !nodes.empty() && root < nodes.size();
  }
};
inline constexpr std::size_t max_nodes = 128;
inline constexpr int max_parse_depth = 24;

// Verify all untrusted or externally constructed ASTs before evaluation or
// code emission. A valid postorder AST has the root as its final node,
// backward-only edges, no detached nodes, and recognized operation tags.
[[nodiscard]] inline bool validate_ast(const Parsed& p) {
  if (!p.ok() || p.nodes.size() > max_nodes ||
      p.root != p.nodes.size() - 1) return false;
  std::vector<unsigned char> reachable(p.nodes.size(), 0);
  std::vector<std::size_t> pending{p.root};
  while (!pending.empty()) {
    const auto index = pending.back();
    pending.pop_back();
    if (index >= p.nodes.size()) return false;
    if (reachable[index]) continue;
    reachable[index] = 1;
    const auto& node = p.nodes[index];
    switch (node.op) {
      case Op::literal:
        if (node.left != 0 || node.right != 0) return false;
        break;
      case Op::neg:
      case Op::bnot:
      case Op::inline_task:
        if (node.left >= index || node.right != 0) return false;
        pending.push_back(node.left);
        break;
      case Op::add:
      case Op::sub:
      case Op::mul:
      case Op::div:
      case Op::rem:
      case Op::band:
      case Op::bor:
      case Op::bxor:
      case Op::shl:
      case Op::lshr:
      case Op::ashr:
      case Op::seq:
        if (node.left >= index || node.right >= index) return false;
        pending.push_back(node.left);
        pending.push_back(node.right);
        break;
      default: return false;
    }
  }
  for (auto seen : reachable) if (!seen) return false;
  return true;
}

class Parser {
  std::string_view source_;
  std::vector<Token> tokens_;
  std::size_t pos_ = 0;
  Parsed out_;
  [[nodiscard]] const Token& current() const { return tokens_[pos_]; }
  [[nodiscard]] std::string_view text() const {
    const auto& t = current();
    return source_.substr(t.offset, t.length);
  }
  void fail(ParseError error) {
    if (out_.error == ParseError::none) {
      out_.error = error;
      out_.error_offset = current().offset;
    }
  }
  [[nodiscard]] bool take(Kind kind) {
    if (current().kind != kind) return false;
    ++pos_;
    return true;
  }
  [[nodiscard]] bool name(std::string_view word) {
    if (current().kind != Kind::name || text() != word) return false;
    ++pos_;
    return true;
  }
  // Every expression in this restricted scalar frontend evaluates to i64;
  // infer therefore resolves to i64 without permitting dynamic values.
  [[nodiscard]] bool scalar_return_type() {
    return name("int") || name("infer");
  }
  [[nodiscard]] std::optional<std::size_t> append(Node n) {
    if (out_.nodes.size() >= max_nodes) {
      fail(ParseError::node_limit);
      return std::nullopt;
    }
    out_.nodes.push_back(n);
    return out_.nodes.size() - 1;
  }
  // bit_or < bit_xor (^^ only) < bit_and < addition < multiplication < unary.
  // Flat precedence tiers do not consume the nesting depth budget.
  // Only actual parenthesized or unary nesting increments depth.
  [[nodiscard]] std::optional<std::size_t> expr(int depth) {
    return parse_or(depth);
  }
  [[nodiscard]] std::optional<std::size_t> parse_or(int depth) {
    auto lhs = parse_xor(depth);
    if (!lhs) return std::nullopt;
    while (take(Kind::bit_or)) {
      auto rhs = parse_xor(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{Op::bor, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  [[nodiscard]] std::optional<std::size_t> parse_xor(int depth) {
    auto lhs = parse_and(depth);
    if (!lhs) return std::nullopt;
    while (take(Kind::bit_xor)) {
      auto rhs = parse_and(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{Op::bxor, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  [[nodiscard]] std::optional<std::size_t> parse_and(int depth) {
    auto lhs = parse_shift(depth);
    if (!lhs) return std::nullopt;
    while (take(Kind::bit_and)) {
      auto rhs = parse_shift(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{Op::band, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  [[nodiscard]] std::optional<std::size_t> parse_shift(int depth) {
    auto lhs = additive(depth);
    if (!lhs) return std::nullopt;
    while (current().kind == Kind::shl || current().kind == Kind::lshr ||
           (current().kind == Kind::name && text() == "ashr")) {
      const auto op = current().kind == Kind::shl ? Op::shl :
                      current().kind == Kind::lshr ? Op::lshr : Op::ashr;
      ++pos_;
      auto rhs = additive(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{op, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  [[nodiscard]] std::optional<std::size_t> additive(int depth) {
    if (depth > max_parse_depth) { fail(ParseError::depth_limit); return std::nullopt; }
    auto lhs = term(depth);
    if (!lhs) return std::nullopt;
    while (current().kind == Kind::plus || current().kind == Kind::minus) {
      const auto op = current().kind == Kind::plus ? Op::add : Op::sub;
      ++pos_;
      auto rhs = term(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{op, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  [[nodiscard]] std::optional<std::size_t> term(int depth) {
    if (depth > max_parse_depth) { fail(ParseError::depth_limit); return std::nullopt; }
    auto lhs = unary(depth);
    if (!lhs) return std::nullopt;
    while (current().kind == Kind::star || current().kind == Kind::slash ||
           current().kind == Kind::percent) {
      Op op = Op::mul;
      if (current().kind == Kind::slash) op = Op::div;
      if (current().kind == Kind::percent) op = Op::rem;
      ++pos_;
      auto rhs = unary(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{op, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  // Sequence prefixes remain checked postorder dependencies, never discarded.
  [[nodiscard]] std::optional<std::size_t> sequence_body(int depth) {
    if (depth > max_parse_depth) { fail(ParseError::depth_limit); return std::nullopt; }
    auto last = expr(depth + 1);
    if (!last) return std::nullopt;
    std::size_t count = 1;
    while (take(Kind::comma)) {
      if (++count > 16) { fail(ParseError::node_limit); return std::nullopt; }
      auto next = expr(depth + 1);
      if (!next) return std::nullopt;
      last = append(Node{Op::seq, *last, *next, 0});
      if (!last) return std::nullopt;
    }
    if (!take(Kind::rparen)) { fail(ParseError::syntax); return std::nullopt; }
    return last;
  }
  [[nodiscard]] std::optional<std::size_t> unary(int depth) {
    if (depth > max_parse_depth) { fail(ParseError::depth_limit); return std::nullopt; }
    if (take(Kind::plus)) return unary(depth + 1);
    if (take(Kind::tilde)) {
      auto rhs = unary(depth + 1);
      if (!rhs) return std::nullopt;
      return append(Node{Op::bnot, *rhs, 0, 0});
    }
    if (take(Kind::minus)) {
      // Literal INT64_MIN is allowed; negating any evaluated INT64_MIN
      // is a checked overflow in the subsequent evaluator/codegen.
      if (current().kind == Kind::integer && text().size() < 32) {
        // The negative range includes |INT64_MIN|. Reuse the canonical
        // decimal validator instead of comparing only the unpadded token.
        // This accepts -0009223372036854775808 consistently with other
        // decimal leading-zero literals but rejects larger magnitudes.
        const std::string signed_digits = "-" + std::string(text());
        const auto minimum = parse_decimal_i64(signed_digits);
        if (minimum && *minimum == std::numeric_limits<i64>::min()) {
          ++pos_;
          return append(Node{Op::literal, 0, 0, *minimum});
        }
      }
      auto rhs = unary(depth + 1);
      if (!rhs) return std::nullopt;
      return append(Node{Op::neg, *rhs, 0, 0});
    }
    if (name("seq")) {
      if (!take(Kind::lparen)) { fail(ParseError::syntax); return std::nullopt; }
      return sequence_body(depth + 1);
    }
    if (take(Kind::lparen)) {
      if (take(Kind::gt)) return sequence_body(depth + 1);
      auto inside = expr(depth + 1);
      if (!inside) return std::nullopt;
      if (!take(Kind::rparen)) { fail(ParseError::syntax); return std::nullopt; }
      return inside;
    }
    if (current().kind == Kind::name && text() == "sync")
      return inline_task(depth + 1, false);
    if (current().kind == Kind::integer) {
      auto n = parse_decimal_i64(text());
      if (!n) { fail(ParseError::literal_out_of_range); return std::nullopt; }
      ++pos_;
      return append(Node{Op::literal, 0, 0, *n});
    }
    fail(ParseError::syntax);
    return std::nullopt;
  }
  // Pure restricted task: same-frame, checked scalar execution. No child,
  // suspension, future, cancellation or ambient actor capability is granted.
  [[nodiscard]] std::optional<std::size_t> inline_task(int depth, bool discard) {
    if (depth > max_parse_depth) { fail(ParseError::depth_limit); return std::nullopt; }
    if (!name("sync") || !name("task")) {
      fail(ParseError::syntax); return std::nullopt;
    }
    enum class Payload { inferred, integer, void_type };
    Payload payload = Payload::inferred;
    if (take(Kind::lt)) {
      if (name("int")) payload = Payload::integer;
      else if (name("void")) payload = Payload::void_type;
      else { fail(ParseError::task_unsupported); return std::nullopt; }
      if (!take(Kind::gt)) { fail(ParseError::task_unsupported); return std::nullopt; }
    }
    if (!take(Kind::lbrace)) { fail(ParseError::syntax); return std::nullopt; }
    std::optional<std::size_t> body;
    if (name("return")) {
      if (current().kind != Kind::semicolon) body = expr(depth + 1);
      if (out_.error != ParseError::none) return std::nullopt;
      if (!take(Kind::semicolon)) { fail(ParseError::syntax); return std::nullopt; }
    }
    if (!take(Kind::rbrace)) { fail(ParseError::syntax); return std::nullopt; }
    if ((discard && (payload == Payload::integer || body.has_value())) ||
        (!discard && (payload == Payload::void_type || !body))) {
      fail(ParseError::task_return_type); return std::nullopt;
    }
    // A typed void statement uses an unobservable scalar placeholder;
    // the checker still requires all referenced child nodes to be reachable.
    if (!body) body = append(Node{Op::literal, 0, 0, 0});
    if (!body) return std::nullopt;
    return append(Node{Op::inline_task, *body, 0, 0});
  }
public:
  Parser(std::string_view source, Lexed lexed)
      : source_(source), tokens_(std::move(lexed.tokens)) {
    if (!lexed.ok()) {
      out_.error = lexed.error == LexError::ownership_bang_unsupported
          ? ParseError::ownership_bang_unsupported : ParseError::lexical;
      out_.error_offset = lexed.error_offset;
    }
    if (out_.error == ParseError::none) {
      for (const auto& token : tokens_) {
        if (token.kind == Kind::io_write || token.kind == Kind::io_read) {
          out_.error = ParseError::io_direction_unsupported;
          out_.error_offset = token.offset;
          break;
        }
      }
    }
  }
  [[nodiscard]] Parsed parse() {
    if (out_.error != ParseError::none) return std::move(out_);
    (void)name("pub");
    const bool fnc = name("fnc");
    bool signature_ok = false;
    if (scalar_return_type()) {
      // Prefix int/infer return type, with or without fnc.
      signature_ok = take(Kind::left_arrow) && name("main") &&
          take(Kind::lparen) && take(Kind::rparen);
    } else if (fnc && name("main") && take(Kind::lparen) &&
               take(Kind::rparen)) {
      // Accept colon return type, arrow return type, or block arrow.
      if (take(Kind::colon)) {
        signature_ok = scalar_return_type();
        if (signature_ok) (void)take(Kind::arrow);
      } else if (take(Kind::arrow)) {
        signature_ok = scalar_return_type();
      }
    }
    if (!signature_ok) {
      fail(ParseError::syntax);
      return std::move(out_);
    }
    const bool brace_body = take(Kind::lbrace);
    if (!brace_body) {
      if (!name("begin")) {
        fail(ParseError::syntax);
        return std::move(out_);
      }
      // begin must be a standalone next-line marker in this scalar profile.
      const auto& begin = tokens_[pos_ - 1];
      const auto& prior = tokens_[pos_ - 2];
      const auto before = source_.substr(prior.offset + prior.length,
                                          begin.offset - prior.offset - prior.length);
      const auto& first_statement = current();
      const auto after = source_.substr(begin.offset + begin.length,
                                         first_statement.offset - begin.offset - begin.length);
      if (before.find('\n') == std::string_view::npos ||
          after.find('\n') == std::string_view::npos) {
        fail(ParseError::syntax);
        return std::move(out_);
      }
    }
    // The only statement form added here is an in-frame, void-only
    // 'do sync task'. Its completion precedes the final scalar return.
    std::optional<std::size_t> before_return;
    while (current().kind == Kind::name && text() == "do") {
      (void)name("do");
      auto t = inline_task(1, true);
      if (!t) return std::move(out_);
      (void)take(Kind::semicolon);
      if (before_return) {
        before_return = append(Node{Op::seq, *before_return, *t, 0});
        if (!before_return) return std::move(out_);
      } else before_return = *t;
    }
    if (!(name("return") || take(Kind::caret_return))) {
      fail(ParseError::syntax);
      return std::move(out_);
    }
    auto root = expr(0);
    if (!root) return std::move(out_);
    if (!take(Kind::semicolon)) {
      fail(ParseError::syntax);
      return std::move(out_);
    }
    if (brace_body) {
      if (!take(Kind::rbrace)) {
        fail(ParseError::syntax);
        return std::move(out_);
      }
    } else {
      if (!name("end")) {
        fail(ParseError::syntax);
        return std::move(out_);
      }
      (void)take(Kind::period);
      (void)take(Kind::semicolon);
    }
    if (current().kind != Kind::end) {
      fail(ParseError::syntax);
      return std::move(out_);
    }
    if (before_return) {
      root = append(Node{Op::seq, *before_return, *root, 0});
      if (!root) return std::move(out_);
    }
    out_.root = *root;
    return std::move(out_);
  }
};

[[nodiscard]] inline Parsed parse_source(std::string_view src) {
  Lexed lexed = tokenize(src);
  // Token-based reservations ignore text inside comments. Only the
  // lexically adjacent 'sync task' form can create the checked inline node.
  if (lexed.ok()) {
    auto word_at = [&](std::size_t i) -> std::string_view {
      if (i >= lexed.tokens.size()) return {};
      const auto& tok = lexed.tokens[i];
      return tok.kind == Kind::name
          ? src.substr(tok.offset, tok.length) : std::string_view{};
    };
    for (std::size_t i = 0; i < lexed.tokens.size(); ++i) {
      const auto word = word_at(i);
      Parsed rejected;
      rejected.error_offset = lexed.tokens[i].offset;
      if (word == "pipe" || word == "rpipe")
        rejected.error = ParseError::unsupported_continuous_pipe;
      else if (word == "sync" && word_at(i + 1) != "task")
        rejected.error = ParseError::reserved_sync;
      else if (word == "task" && (i == 0 || word_at(i - 1) != "sync"))
        rejected.error = ParseError::task_unsupported;
      if (rejected.error != ParseError::none) return rejected;
    }
  }
  return Parser(src, std::move(lexed)).parse();
}
}  // namespace atto::forge::scalar
