#pragma once
// Compile-time typed std.pattern fragment admission: NATIVE HOST REFERENCE ONLY.
// This is NOT the Forge .atto compiler's typed pattern AST or guest codegen.
// No arbitrary user extractors, host callbacks, filesystem, or runtime parsing.
#include <charconv>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace atto::forge::pattern {
enum class Type { any, integer, boolean, text, floating };
enum class Error { none, context, constexpr_required, type, source, budget };
enum class Kind { one, rest, int_literal, bool_literal, string_literal, is_type };
struct Atom {
  Kind kind = Kind::one;
  Type type = Type::any;
  std::int64_t integer = 0;
  bool boolean = false;
  bool negative = false;
  std::string text;
};
struct Fragment {
  Type type = Type::any;
  std::vector<Atom> items;
};
struct Compiled {
  Fragment fragment;
  Error error = Error::none;
  [[nodiscard]] bool ok() const noexcept { return error == Error::none; }
};
inline constexpr std::size_t max_source_bytes = 4096;
inline constexpr std::size_t max_items = 128;
inline constexpr std::size_t max_input_elements = 256;
inline constexpr std::size_t max_dp_states = (max_items + 1) * (max_input_elements + 1);

[[nodiscard]] constexpr std::string_view error_name(Error e) noexcept {
  switch (e) {
    case Error::none: return "NONE";
    case Error::context: return "E_PATTERN_CONTEXT";
    case Error::constexpr_required: return "E_PATTERN_CONSTEXPR";
    case Error::type: return "E_PATTERN_TYPE";
    case Error::source: return "E_PATTERN_SOURCE";
    case Error::budget: return "E_PATTERN_BUDGET";
  }
  return "E_PATTERN_UNKNOWN";
}

[[nodiscard]] inline std::string_view trim(std::string_view s) noexcept {
  auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && ws(s.front())) s.remove_prefix(1);
  while (!s.empty() && ws(s.back())) s.remove_suffix(1);
  return s;
}

[[nodiscard]] inline std::optional<Type> named_type(std::string_view s) {
  if (s == "Any") return Type::any;
  if (s == "int") return Type::integer;
  if (s == "bool") return Type::boolean;
  if (s == "string") return Type::text;
  if (s == "float") return Type::floating;
  return std::nullopt;
}

// No nesting beyond a single counted group; nested function calls are not
// a pattern language. Quotes protect embedded commas and parentheses.
[[nodiscard]] inline Error split(std::string_view s, std::vector<std::string_view>& out) {
  bool quoted = false, escaped = false;
  int depth = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char ch = s[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (ch == '\\') escaped = true;
      else if (ch == '"') quoted = false;
      continue;
    }
    if (ch == '"') quoted = true;
    else if (ch == '(') { if (++depth > 1) return Error::source; }
    else if (ch == ')') { if (--depth < 0) return Error::source; }
    else if (ch == ',' && depth == 0) {
      const auto part = trim(s.substr(start, i - start));
      if (part.empty()) return Error::source;
      out.push_back(part);
      if (out.size() > max_items) return Error::budget;
      start = i + 1;
    }
  }
  if (quoted || escaped || depth != 0) return Error::source;
  const auto last = trim(s.substr(start));
  if (last.empty()) return Error::source;
  out.push_back(last);
  return out.size() > max_items ? Error::budget : Error::none;
}

[[nodiscard]] inline bool source_is_safe(std::string_view s) noexcept {
  // This first native reference slice admits ASCII-only source. Non-ASCII
  // fragment text is E_PATTERN_SOURCE until UTF-8 / bidi contracts are
  // independently implemented. No false Unicode parity claim.
  for (const unsigned char ch : s) {
    if (ch == 0 || ch >= 0x80) return false;
  }
  return true;
}

[[nodiscard]] inline Error atom(std::string_view text, Type expected, Atom& out) {
  text = trim(text);
  if (text == "_") { out.kind = Kind::one; return Error::none; }
  if (text == "...") { out.kind = Kind::rest; return Error::none; }
  if (text.starts_with("is ")) {
    auto type_name = trim(text.substr(3));
    bool negated = false;
    if (type_name.starts_with("!")) {
      type_name.remove_prefix(1);
      type_name = trim(type_name);
      negated = true;
    }
    const auto ty = named_type(type_name);
    if (!ty || *ty == Type::any) return Error::type;
    if (expected != Type::any &&
        ((!negated && expected != *ty) || (negated && expected == *ty))) return Error::type;
    out.kind = Kind::is_type;
    out.type = *ty;
    out.negative = negated;
    return Error::none;
  }
  if (text == "true" || text == "false") {
    if (expected != Type::any && expected != Type::boolean) return Error::type;
    out.kind = Kind::bool_literal;
    out.boolean = (text == "true");
    return Error::none;
  }
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
    if (expected != Type::any && expected != Type::text) return Error::type;
    out.kind = Kind::string_literal;
    const auto body = text.substr(1, text.size()-2);
    for (std::size_t i=0; i<body.size(); ++i) {
      const char ch = body[i];
      if (ch == '\\') {
        if (++i >= body.size()) return Error::source;
        switch (body[i]) {
          case '"': out.text.push_back('"'); break;
          case '\\': out.text.push_back('\\'); break;
          case 'n': out.text.push_back('\n'); break;
          case 't': out.text.push_back('\t'); break;
          default: return Error::source;
        }
      } else {
        if (ch == '"' || static_cast<unsigned char>(ch) < 32) return Error::source;
        out.text.push_back(ch);
      }
    }
    return Error::none;
  }
  if (!text.empty() && (text.front() == '-' || (text.front() >= '0' && text.front() <= '9'))) {
    std::int64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data()+text.size(), value);
    if (ec == std::errc{} && end == text.data()+text.size()) {
      if (text.starts_with("-0") || (text.starts_with("0") && text.size() > 1)) return Error::source;
      if (expected != Type::any && expected != Type::integer) return Error::type;
      out.kind = Kind::int_literal;
      out.integer = value;
      return Error::none;
    }
  }
  return Error::source; // no user code, binding, getters or nested calls
}

[[nodiscard]] inline Compiled compile(
    std::string_view source, Type contextual, std::optional<Type> explicit_type,
    bool canonical_symbol, bool pattern_position, bool constexpr_known) {
  Compiled out;
  out.fragment.type = contextual;
  if (!canonical_symbol || !pattern_position) { out.error = Error::context; return out; }
  if (!constexpr_known) { out.error = Error::constexpr_required; return out; }
  if (explicit_type && *explicit_type != contextual) { out.error = Error::type; return out; }
  if (source.size() > max_source_bytes) { out.error = Error::budget; return out; }
  if (!source_is_safe(source)) { out.error = Error::source; return out; }
  std::vector<std::string_view> pieces;
  out.error = split(source,pieces);
  if (!out.ok()) return out;
  for (auto piece : pieces) {
    if (piece.starts_with("...(") && piece.ends_with(")")) {
      std::vector<std::string_view> groups;
      out.error = split(piece.substr(4,piece.size()-5),groups);
      if (!out.ok()) return out;
      if (groups.size() > 32) { out.error=Error::budget; return out; }
      for (auto group : groups) {
        const auto of = group.find(" of ");
        if (of == std::string_view::npos) { out.error=Error::source; return out; }
        const auto number = group.substr(0,of);
        const auto value = trim(group.substr(of+4));
        if (number.empty() || (number.size()>1 && number.front()=='0')) {
          out.error=Error::source; return out;
        }
        std::size_t n=0;
        const auto [end,ec] = std::from_chars(number.data(),number.data()+number.size(),n);
        if (ec == std::errc::result_out_of_range) { out.error=Error::budget; return out; }
        if (ec != std::errc{} || end != number.data()+number.size()) {
          out.error=Error::source; return out;
        }
        if (n > max_items-out.fragment.items.size()) {
          out.error=Error::budget; return out;
        }
        Atom single;
        auto ty = named_type(value);
        if (ty && *ty != Type::any) {
          out.error = atom(value == "int" ? "is int" :
                           value == "bool" ? "is bool" :
                           value == "float" ? "is float" : "is string",
                           contextual,single);
        } else out.error=atom(value,contextual,single);
        if (!out.ok()) return out;
        if (single.kind == Kind::rest) { out.error=Error::source; return out; }
        for (std::size_t i=0; i<n; ++i) out.fragment.items.push_back(single);
      }
    } else {
      if (out.fragment.items.size()>=max_items) { out.error=Error::budget; return out; }
      Atom next;
      out.error=atom(piece,contextual,next);
      if (!out.ok()) return out;
      out.fragment.items.push_back(std::move(next));
    }
  }
  for (std::size_t i=1;i<out.fragment.items.size();++i) {
    if (out.fragment.items[i-1].kind==Kind::rest && out.fragment.items[i].kind==Kind::rest) {
      out.error=Error::source; return out;
    }
  }
  return out;
}

// Combine pure compile-time fragments after contextual type checking.
// Adjacent-rest restrictions apply after expansion, not merely per string.
[[nodiscard]] inline Compiled compose(const std::vector<Fragment>& parts, Type type) {
  Compiled out;
  out.fragment.type=type;
  for (const auto& p : parts) {
    if (p.type != type) { out.error=Error::type; return out; }
    if (p.items.size() > max_items-out.fragment.items.size()) {
      out.error=Error::budget; return out;
    }
    out.fragment.items.insert(out.fragment.items.end(),p.items.begin(),p.items.end());
  }
  for (std::size_t i=1;i<out.fragment.items.size();++i) {
    if (out.fragment.items[i-1].kind==Kind::rest &&
        out.fragment.items[i].kind==Kind::rest) {
      out.error=Error::source; return out;
    }
  }
  return out;
}

[[nodiscard]] inline bool matches_i64(const Atom& a, std::int64_t x) noexcept {
  switch (a.kind) {
    case Kind::one: return true;
    case Kind::int_literal: return a.integer == x;
    case Kind::is_type: return (a.type == Type::integer) != a.negative;
    default: return false;
  }
}

struct Witness {
  Error error = Error::none;
  bool matched = false;
  std::vector<std::pair<std::size_t,std::size_t>> rest_regions;
};
// Leftmost-shortest, O(n*m), no callbacks or exponential backtracking.
[[nodiscard]] inline Witness reference_match_i64(
    const Fragment& pattern, const std::vector<std::int64_t>& input) {
  Witness out;
  if (pattern.type != Type::integer) { out.error=Error::type; return out; }
  const auto m=pattern.items.size(), n=input.size();
  if (m>max_items || n>max_input_elements || (m+1)*(n+1)>max_dp_states) {
    out.error=Error::budget; return out;
  }
  const auto width=n+1;
  std::vector<unsigned char> dp((m+1)*width,0);
  dp[m*width+n]=1;
  for (std::size_t i=m;i-->0;) {
    for (std::size_t j=n+1;j-->0;) {
      const auto& p=pattern.items[i];
      dp[i*width+j]= static_cast<unsigned char>(
        p.kind==Kind::rest
          ? dp[(i+1)*width+j] || (j<n && dp[i*width+j+1])
          : j<n && matches_i64(p,input[j]) && dp[(i+1)*width+j+1]);
    }
  }
  if (!dp[0]) return out;
  out.matched=true;
  std::size_t j=0;
  for (std::size_t i=0;i<m;++i) {
    if (pattern.items[i].kind==Kind::rest) {
      const auto start=j;
      while (!dp[(i+1)*width+j]) ++j;
      out.rest_regions.emplace_back(start,j);
    } else ++j;
  }
  return out;
}
}  // namespace atto::forge::pattern
