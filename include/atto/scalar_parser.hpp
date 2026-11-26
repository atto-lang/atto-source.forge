#pragma once
// Restricted native Atto scalar parser: one main() with a scalar return.
#include "atto/scalar_lexer.hpp"
#include "atto/decimal_i64.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atto::forge::scalar {
enum class Op { literal, add, sub, mul, div, rem, neg, band, bor, bxor, bnot, shl, lshr, ashr };
struct Node {
  Op op;
  std::size_t left = 0, right = 0;
  i64 value = 0;
};
enum class ParseError {
  none, lexical, syntax, literal_out_of_range, depth_limit, node_limit, telemetry
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
    case ParseError::telemetry: return "E_TELEMETRY_SCOPE";
  }
  return "E_PARSE_UNKNOWN";
}

struct Parsed {
  std::vector<Node> nodes;
  std::size_t root = 0;
  ParseError error = ParseError::none;
  std::size_t error_offset = 0;
  // Named entries open a scope. Empty entries close the innermost scope.
  // Remaining scopes close with the terminal scalar result or checked error.
  std::vector<std::optional<std::string>> trace_steps;
  [[nodiscard]] bool ok() const noexcept {
    return error == ParseError::none && !nodes.empty() && root < nodes.size();
  }
};
inline constexpr std::size_t max_nodes = 128;
inline constexpr int max_parse_depth = 24;
inline constexpr std::size_t max_trace_depth = 16;

[[nodiscard]] inline bool valid_operation(std::string_view operation) {
  if (operation.empty() || operation.size() > 256) return false;
  for (unsigned char byte : operation) {
    if (byte < 32 || byte > 126 || byte == '"' || byte == '\\') return false;
  }
  return true;
}

// Verify all untrusted or externally constructed ASTs before evaluation or
// code emission. A valid postorder AST has the root as its final node,
// backward-only edges, no detached nodes, and recognized operation tags.
[[nodiscard]] inline bool validate_ast(const Parsed& p) {
  if (!p.ok() || p.nodes.size() > max_nodes ||
      p.root != p.nodes.size() - 1) return false;
  if (p.trace_steps.size() > max_tokens) return false;
  std::size_t trace_depth = 0;
  for (const auto& operation : p.trace_steps) {
    if (operation) {
      if (!valid_operation(*operation) || ++trace_depth > max_trace_depth) return false;
    } else {
      if (trace_depth == 0) return false;
      --trace_depth;
    }
  }
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
  // File-local bounded rational dimensions; erased only after scalar admission.
  using Dim = std::array<int, 8>;
  struct Scale {
    i64 n = 1, d = 1;
    bool operator==(const Scale&) const = default;
    [[nodiscard]] Scale inv() const { return {d,n}; }
    [[nodiscard]] std::optional<Scale> mul(Scale rhs) const {
      const i64 a=std::gcd(n,rhs.d), b=std::gcd(rhs.n,d);
      const i64 x=n/a,y=rhs.n/b,z=d/b,w=rhs.d/a;
      constexpr auto max=std::numeric_limits<i64>::max();
      if (x>max/y || z>max/w) return std::nullopt;
      return Scale{x*y,z*w};
    }
    [[nodiscard]] std::optional<i64> convert(i64 value,Scale target) const {
      // Zero is exact for every valid positive ratio, even when the
      // intermediate scale ratio itself exceeds this profile's i64 bound.
      if (value==0) return 0;
      const auto ratio=mul(target.inv());
      if (!ratio || value%ratio->d != 0) return std::nullopt;
      const i64 q=value/ratio->d;
      if (q>std::numeric_limits<i64>::max()/ratio->n ||
          q<std::numeric_limits<i64>::min()/ratio->n) return std::nullopt;
      return q*ratio->n;
    }
  };
  std::vector<Dim> dims_;
  std::vector<Scale> scales_;
  std::vector<std::string_view> dimensions_;
  struct Unit { std::string_view name; std::size_t dim; Scale scale; };
  std::vector<Unit> units_;
  static constexpr int exponent_limit = 32;
  [[nodiscard]] bool dimensionless(std::size_t n) const { return dims_[n] == Dim{} && scales_[n] == Scale{}; }
  [[nodiscard]] bool known_name(std::string_view n) const {
    for (auto d : dimensions_) if (d == n) return true;
    for (auto u : units_) if (u.name == n) return true;
    return false;
  }
  [[nodiscard]] bool declaration_name(std::string_view n) const {
    // This bounded profile uses conventional upper-case nominal names.
    return !n.empty() && n[0] >= 'A' && n[0] <= 'Z' && !known_name(n);
  }
  [[nodiscard]] std::optional<i64> factor() {
    if (current().kind!=Kind::integer) return std::nullopt;
    auto value=parse_decimal_i64(text());
    if (!value || *value<=0) return std::nullopt;
    ++pos_; return value;
  }
  [[nodiscard]] bool declarations() {
    while (name("define")) {
      const bool dimension = name("dimension");
      if ((!dimension && !name("unit")) || current().kind != Kind::name || !declaration_name(text())) return false;
      const auto n = text(); ++pos_;
      if (dimension) {
        if (dimensions_.size() == Dim{}.size()) return false;
        dimensions_.push_back(n);
      } else {
        if (units_.size() == 32 || !take(Kind::colon) || current().kind != Kind::name) return false;
        std::size_t d = 0;
        while (d < dimensions_.size() && dimensions_[d] != text()) ++d;
        if (d == dimensions_.size()) return false;
        ++pos_;
        Scale scale{};
        if (take(Kind::equals)) {
          Scale f{};
          const bool prefix=current().kind==Kind::integer;
          if (prefix) {
            const auto k=factor(); if (!k || !take(Kind::star)) return false;
            f.n=*k;
          }
          if (current().kind != Kind::name) return false;
          const Unit* base=nullptr;
          for (const auto& u : units_) if (u.name==text() && u.dim==d) base=&u;
          if (!base) return false;
          ++pos_;
          if (!prefix && (current().kind==Kind::star || current().kind==Kind::slash)) {
            const bool divide=take(Kind::slash);
            if (!divide) (void)take(Kind::star);
            const auto k=factor(); if (!k) return false;
            if (divide) f.d=*k; else f.n=*k;
          }
          const auto normalized=base->scale.mul(f);
          if (!normalized) return false;
          scale=*normalized;
        } else {
          for (auto u : units_) if (u.dim == d) return false;
        }
        units_.push_back(Unit{n,d,scale});
      }
      if (!take(Kind::semicolon)) return false;
    }
    return true;
  }
  [[nodiscard]] bool attach(std::size_t n) {
    if (!dimensionless(n) || current().kind != Kind::name) { fail(ParseError::syntax); return false; }
    for (const auto& u : units_) if (u.name == text()) {
      dims_[n][u.dim] = 1; scales_[n]=u.scale; ++pos_; return true;
    }
    fail(ParseError::syntax); return false;
  }
  [[nodiscard]] bool scalar_cast(std::size_t n) {
    if (dimensionless(n)) return true;
    fail(ParseError::syntax); return false;
  }
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
    Dim d{};
    Scale s{};
    if (n.op != Op::literal) {
      const auto& lhs = dims_[n.left];
      if (n.op == Op::neg) { d = lhs; s = scales_[n.left]; }
      else if (n.op == Op::bnot) {
        if (!dimensionless(n.left)) { fail(ParseError::syntax); return std::nullopt; }
      } else {
        const auto& rhs = dims_[n.right];
        if (n.op == Op::add || n.op == Op::sub) {
          if (lhs != rhs || scales_[n.left]!=scales_[n.right]) { fail(ParseError::syntax); return std::nullopt; }
          d = lhs; s=scales_[n.left];
        } else if (n.op == Op::mul || n.op == Op::div) {
          const auto scale=scales_[n.left].mul(n.op==Op::div?scales_[n.right].inv():scales_[n.right]);
          if (!scale) { fail(ParseError::syntax); return std::nullopt; }
          s=*scale;
          for (std::size_t i = 0; i < d.size(); ++i) {
            d[i] = lhs[i] + (n.op == Op::mul ? rhs[i] : -rhs[i]);
            if (d[i] > exponent_limit || d[i] < -exponent_limit) { fail(ParseError::syntax); return std::nullopt; }
          }
        } else if (!dimensionless(n.left) || !dimensionless(n.right)) {
          fail(ParseError::syntax); return std::nullopt;
        }
      }
    }
    dims_.push_back(d); scales_.push_back(s);
    out_.nodes.push_back(n);
    return out_.nodes.size() - 1;
  }
  // bit_or < bit_xor < bit_and < addition < multiplication < unary.
  // Flat precedence tiers do not consume the nesting depth budget.
  // Only actual parenthesized or unary nesting increments depth.
  [[nodiscard]] std::optional<std::size_t> expr(int depth) {
    auto result = parse_or(depth);
    if (!result) return std::nullopt;
    while (name("as")) {
      if (current().kind == Kind::name && (text() == "int" || text() == "i64")) {
        if (!cast_target() || !scalar_cast(*result)) return std::nullopt;
      } else if (!attach(*result)) return std::nullopt;
    }
    return result;
  }
  // All admitted expressions are signed i64. These casts validate the
  // carrier and preserve the existing node, including checked arithmetic.
  // Reject every other carrier and unit; no unchecked erasure.
  [[nodiscard]] bool cast_target() {
    if (name("int") || name("i64")) return true;
    fail(ParseError::syntax);
    return false;
  }
  [[nodiscard]] std::optional<std::size_t> postfix(std::optional<std::size_t> result, bool compact = false, bool conversion = false) {
    if (!result) return std::nullopt;
    while (current().kind == Kind::double_colon || current().kind == Kind::dot ||
           (compact && current().kind == Kind::lt && pos_ > 0 &&
            tokens_[pos_-1].offset + tokens_[pos_-1].length == current().offset)) {
      const bool cast = take(Kind::double_colon);
      if (!cast && take(Kind::dot) && name("to_unit")) {
        if (!conversion || out_.nodes[*result].op!=Op::literal || !take(Kind::lt) || current().kind!=Kind::name) {
          fail(ParseError::syntax); return std::nullopt;
        }
        const Unit* target=nullptr;
        for (const auto& u:units_) if (u.name==text()) target=&u;
        if (!target) { fail(ParseError::syntax); return std::nullopt; }
        Dim d{}; d[target->dim]=1;
        if (dims_[*result]!=d) { fail(ParseError::syntax); return std::nullopt; }
        ++pos_;
        if (!take(Kind::gt)||!take(Kind::lparen)||!take(Kind::rparen)) { fail(ParseError::syntax); return std::nullopt; }
        const auto converted=scales_[*result].convert(out_.nodes[*result].value,target->scale);
        if (!converted) { fail(ParseError::syntax); return std::nullopt; }
        out_.nodes[*result].value=*converted; scales_[*result]=target->scale;
        conversion=false;compact=false;continue;
      }
      if (!take(Kind::lt)) { fail(ParseError::syntax); return std::nullopt; }
      if (cast && current().kind == Kind::name && (text() == "int" || text() == "i64")) {
        if (!cast_target() || !scalar_cast(*result)) return std::nullopt;
      } else if (!attach(*result)) return std::nullopt;
      if (!take(Kind::gt)) { fail(ParseError::syntax); return std::nullopt; }
      compact = false;
    }
    return result;
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
  [[nodiscard]] std::optional<std::size_t> unary(int depth) {
    if (depth > max_parse_depth) { fail(ParseError::depth_limit); return std::nullopt; }
    if (take(Kind::lt)) {
      if (!cast_target() || !take(Kind::gt)) {
        fail(ParseError::syntax); return std::nullopt;
      }
      auto operand = unary(depth + 1);
      if (!operand || !scalar_cast(*operand)) return std::nullopt;
      return operand;
    }
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
          return postfix(append(Node{Op::literal, 0, 0, *minimum}), true, true);
        }
      }
      auto rhs = unary(depth + 1);
      if (!rhs) return std::nullopt;
      return append(Node{Op::neg, *rhs, 0, 0});
    }
    if (take(Kind::lparen)) {
      auto inside = expr(depth + 1);
      if (!inside) return std::nullopt;
      if (!take(Kind::rparen)) { fail(ParseError::syntax); return std::nullopt; }
      return postfix(inside);
    }
    if (current().kind == Kind::integer) {
      auto n = parse_decimal_i64(text());
      if (!n) { fail(ParseError::literal_out_of_range); return std::nullopt; }
      ++pos_;
      return postfix(append(Node{Op::literal, 0, 0, *n}), true, true);
    }
    fail(ParseError::syntax);
    return std::nullopt;
  }
  [[nodiscard]] std::optional<std::size_t> body(std::vector<std::string_view>& aliases) {
    std::optional<std::size_t> root;
    while (name("using")) {
      if (root || aliases.size() == max_trace_depth ||
          !name("telemetry") || !take(Kind::dot) || !name("span") ||
          !take(Kind::lparen) || current().kind != Kind::quoted) {
        fail(ParseError::telemetry);
        return std::nullopt;
      }
      const auto operation = text().substr(1, text().size() - 2);
      ++pos_;
      if (!valid_operation(operation) || !take(Kind::rparen) ||
          !name("as") || current().kind != Kind::name) {
        fail(ParseError::telemetry);
        return std::nullopt;
      }
      const auto alias = text();
      ++pos_;
      if (alias == "_" || alias == "self" || !take(Kind::lbrace)) {
        fail(ParseError::telemetry);
        return std::nullopt;
      }
      for (const auto& active_alias : aliases) {
        if (active_alias == alias) {
          fail(ParseError::telemetry);
          return std::nullopt;
        }
      }
      out_.trace_steps.emplace_back(operation);
      aliases.push_back(alias);
      root = body(aliases);
      aliases.pop_back();
      if (out_.error != ParseError::none) return std::nullopt;
      if (!root) out_.trace_steps.emplace_back(std::nullopt);
    }
    if (name("return") || take(Kind::caret)) {
      const auto previous_size = out_.nodes.size();
      auto value = expr(0);
      if (!value || !scalar_cast(*value) || !take(Kind::semicolon)) {
        fail(ParseError::syntax);
        return std::nullopt;
      }
      if (root) {
        // Parse the unreachable fallback after a terminal scope, then
        // discard those unreachable scalar nodes.
        out_.nodes.resize(previous_size);
        dims_.resize(previous_size); scales_.resize(previous_size);
      } else {
        root = value;
      }
    }
    if (!take(Kind::rbrace)) fail(ParseError::syntax);
    return root;
  }
public:
  Parser(std::string_view source, Lexed lexed)
      : source_(source), tokens_(std::move(lexed.tokens)) {
    if (!lexed.ok()) {
      out_.error = ParseError::lexical;
      out_.error_offset = lexed.error_offset;
    }
  }
  [[nodiscard]] Parsed parse() {
    if (out_.error != ParseError::none) return std::move(out_);
    if (!declarations()) { fail(ParseError::syntax); return std::move(out_); }
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
    if (!signature_ok || !take(Kind::lbrace)) {
      fail(ParseError::syntax);
      return std::move(out_);
    }
    std::vector<std::string_view> aliases;
    auto root = body(aliases);
    if (!root || current().kind != Kind::end) {
      fail(ParseError::syntax);
      return std::move(out_);
    }
    out_.root = *root;
    return std::move(out_);
  }
};

[[nodiscard]] inline Parsed parse_source(std::string_view src) {
  return Parser(src, tokenize(src)).parse();
}
}  // namespace atto::forge::scalar
