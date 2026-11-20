#pragma once
// Restricted real .atto -> C++20 -> native guest compiler path for List<int>
// structural matching. NOT the general std.pattern runtime, not a host matcher.
// All source bytes are parsed and checked before generated C++ is emitted;
// names and source snippets are NEVER interpolated into generated code.
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace atto::forge::list_guest {
enum class Error { none, source, unsupported, binding, budget, range, reserved };
[[nodiscard]] constexpr std::string_view error_name(Error value) noexcept {
  switch (value) {
    case Error::none: return "NONE";
    case Error::source: return "E_PATTERN_SOURCE";
    case Error::unsupported: return "E_UNSUPPORTED";
    case Error::binding: return "E_PATTERN_BINDING";
    case Error::budget: return "E_PATTERN_BUDGET";
    case Error::range: return "E_INTEGER_RANGE";
    case Error::reserved: return "E_RESERVED";
  }
  return "E_PATTERN_UNKNOWN";
}
inline constexpr std::size_t max_source = 16 * 1024;
inline constexpr std::size_t max_tokens = 512;
inline constexpr std::size_t max_values = 32;
inline constexpr std::size_t max_fixed = 16;
inline constexpr std::size_t max_output = 32 * 1024;

struct Token { std::string_view text; std::size_t offset = 0; };
struct Lexed {
  std::vector<Token> tokens;
  Error error = Error::none;
  std::size_t offset = 0;
};

// Tokenizer rejects unknown punctuation and arbitrary user code. No strings,
// custom functions, generics besides the exact List<int> and match<int>, or
// calls other than the canonical literal List.of constructor are permitted.
[[nodiscard]] inline Lexed lex(std::string_view source) {
  Lexed out;
  if (source.size() > max_source) {
    out.error = Error::budget; out.offset = max_source; return out;
  }
  auto alpha = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  };
  auto digit = [](char c) { return c >= '0' && c <= '9'; };
  for (std::size_t i = 0; i < source.size();) {
    const char c = source[i];
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { ++i; continue; }
    if (i + 1 < source.size() && source.substr(i, 2) == "//") {
      i += 2;
      while (i < source.size() && source[i] != '\n') ++i;
      continue;
    }
    if (i + 1 < source.size() && source.substr(i, 2) == "/*") {
      i += 2;
      const auto end = source.find("*/", i);
      if (end == std::string_view::npos) {
        out.error = Error::source; out.offset = i - 2; return out;
      }
      i = end + 2; continue;
    }
    if (out.tokens.size() >= max_tokens) {
      out.error = Error::budget; out.offset = i; return out;
    }
    const auto start = i;
    if (alpha(c)) {
      while (i < source.size() && (alpha(source[i]) || digit(source[i]))) ++i;
    } else if (digit(c)) {
      while (i < source.size() && digit(source[i])) ++i;
    } else if (source.substr(i,3) == "...") {
      i += 3;
    } else if (source.substr(i,2) == "->" || source.substr(i,2) == ">=" ||
               source.substr(i,2) == "<=" || source.substr(i,2) == "==" ||
               source.substr(i,2) == "!=") {
      i += 2;
    } else if (std::string_view("(){}:;,<>=.+-_").find(c) != std::string_view::npos) {
      ++i;
    } else {
      out.error = Error::unsupported; out.offset = start; return out;
    }
    out.tokens.push_back({source.substr(start,i-start),start});
  }
  out.tokens.push_back({"<eof>",source.size()});
  return out;
}
enum class AtomKind { any, literal, capture, rest };
struct Atom { AtomKind kind = AtomKind::any; std::int64_t value=0; std::string name; };
struct Term { std::optional<std::size_t> capture; std::int64_t value=0; };
struct Guard { std::size_t capture=0; std::string comparison; std::int64_t value=0; };
struct Program {
  std::vector<std::int64_t> list;
  std::vector<Atom> fixed;
  bool leading=false, trailing=false;
  std::vector<Term> result;
  std::optional<Guard> guard;
  std::int64_t fallback=0;
};
struct Parsed {
  Program program;
  Error error=Error::none;
  std::size_t offset=0;
  [[nodiscard]] bool ok() const noexcept { return error==Error::none; }
};
[[nodiscard]] inline bool reserved(std::string_view s) noexcept {
  // Keep this conservative list in sync with atto-source.rs parser::RESERVED.
  constexpr std::string_view words[] = {
    "fnc","main","pub","val","let","mut","final","const","return",
    "match","over","on","end","is","as","int","bool","string","float",
    "if","then","else","for","while","do","done","switch","default",
    "class","actor","struct","define","import","module","namespace",
    "await","spawn","raise","throw","panic","recover","try","catch"
  };
  for (auto word:words) if (s==word) return true;
  return s=="_";
}
class Parser {
  const std::vector<Token>& ts_;
  std::size_t at_=0;
  Parsed out_;
  [[nodiscard]] std::string_view peek() const { return ts_[at_].text; }
  [[nodiscard]] std::size_t offset() const { return ts_[at_].offset; }
  void fail(Error error) {
    if (out_.ok()) { out_.error=error; out_.offset=offset(); }
  }
  [[nodiscard]] bool eat(std::string_view text) {
    if (peek()!=text) return false;
    if (text!="<eof>") ++at_;
    return true;
  }
  [[nodiscard]] bool need(std::string_view text) {
    if (eat(text)) return true;
    fail(Error::unsupported); return false;
  }
  [[nodiscard]] std::optional<std::string> ident() {
    const auto text=peek();
    if (text.empty() || !((text[0]>='a' && text[0]<='z') ||
           (text[0]>='A' && text[0]<='Z') || text[0]=='_')) {
      fail(Error::unsupported); return std::nullopt;
    }
    if (reserved(text)) { fail(Error::reserved); return std::nullopt; }
    ++at_; return std::string(text);
  }
  [[nodiscard]] std::optional<std::int64_t> number() {
    const auto start=offset();
    const bool negative=eat("-");
    const auto value=peek();
    if (value.empty() || value[0]<'0' || value[0]>'9') {
      fail(Error::range); return std::nullopt;
    }
    std::string signed_text;
    if (negative) signed_text="-";
    signed_text+=value;
    std::int64_t n=0;
    const auto [end,ec]=std::from_chars(signed_text.data(),
        signed_text.data()+signed_text.size(),n);
    if (ec!=std::errc{} || end!=signed_text.data()+signed_text.size()) {
      fail(Error::range); out_.offset=start; return std::nullopt;
    }
    ++at_; return n;
  }
  [[nodiscard]] bool parse_pattern() {
    if (!need("List") || !need(".") || !need("of") || !need("(")) return false;
    std::vector<Atom> sequence;
    while (peek()!=")") {
      if (sequence.size()>=max_fixed+2) { fail(Error::budget); return false; }
      Atom atom;
      if (eat("...")) {
        atom.kind=AtomKind::rest;
      } else if (eat("_")) {
        atom.kind=AtomKind::any;
      } else if (eat("is")) {
        if (!need("int")) return false;
        if (eat("as")) {
          if (!need("val")) return false;
          const auto n=ident();
          if (!n) return false;
          atom.kind=AtomKind::capture;
          atom.name=*n;
        }
      } else {
        const auto value=number();
        if (!value) return false;
        atom.kind=AtomKind::literal; atom.value=*value;
      }
      sequence.push_back(std::move(atom));
      if (peek()==")") break;
      if (!need(",")) return false;
      if (peek()==")") { fail(Error::source); return false; }
    }
    if (!need(")")) return false;
    for (std::size_t i=1;i<sequence.size();++i) {
      if (sequence[i-1].kind==AtomKind::rest &&
          sequence[i].kind==AtomKind::rest) {
        fail(Error::source); return false;
      }
    }
    out_.program.leading=!sequence.empty() && sequence.front().kind==AtomKind::rest;
    out_.program.trailing=sequence.size()>1 && sequence.back().kind==AtomKind::rest;
    std::size_t rests=0;
    for (const auto& atom:sequence) {
      if (atom.kind==AtomKind::rest) { ++rests; continue; }
      if (atom.kind==AtomKind::capture) {
        for (const auto& previous:out_.program.fixed)
          if (previous.kind==AtomKind::capture && previous.name==atom.name) {
            fail(Error::binding); return false;
          }
      }
      out_.program.fixed.push_back(atom);
    }
    if (rests > static_cast<std::size_t>(out_.program.leading) +
                static_cast<std::size_t>(out_.program.trailing)) {
      fail(Error::unsupported); return false;
    }
    if (out_.program.fixed.size()>max_fixed) { fail(Error::budget); return false; }
    return true;
  }
  [[nodiscard]] std::optional<std::size_t> captured(std::string_view text) const {
    for (std::size_t i=0;i<out_.program.fixed.size();++i)
      if (out_.program.fixed[i].kind==AtomKind::capture &&
          out_.program.fixed[i].name==text) return i;
    return std::nullopt;
  }
  [[nodiscard]] bool parse_arm_result() {
    while (true) {
      if (out_.program.result.size()>=max_fixed) {
        fail(Error::budget); return false;
      }
      Term term;
      const auto name=peek();
      if (!name.empty() && ((name[0]>='a' && name[0]<='z') ||
          (name[0]>='A' && name[0]<='Z') || name[0]=='_')) {
        term.capture=captured(name);
        if (!term.capture) { fail(Error::binding); return false; }
        ++at_;
      } else {
        const auto value=number();
        if (!value) return false;
        term.value=*value;
      }
      out_.program.result.push_back(term);
      if (!eat("+")) break;
    }
    return true;
  }
 public:
  explicit Parser(const std::vector<Token>& tokens):ts_(tokens){}
  [[nodiscard]] Parsed parse() {
    if (eat("pub")) { /* explicit visibility is accepted */ }
    if (!need("fnc") || !need("main") || !need("(") ||
        !need(")") || !need(":") || !need("int") || !need("{") ||
        !need("val")) return out_;
    const auto variable=ident();
    if (!variable || !need(":") || !need("List") || !need("<") ||
        !need("int") || !need(">") || !need("=") ||
        !need("List") || !need(".") || !need("of") || !need("("))
      return out_;
    while (peek()!=")") {
      if (out_.program.list.size()>=max_values) {
        fail(Error::budget); return out_;
      }
      const auto item=number();
      if (!item) return out_;
      out_.program.list.push_back(*item);
      if (peek()==")") break;
      if (!need(",")) return out_;
      if (peek()==")") { fail(Error::source); return out_; }
    }
    if (!need(")") || !need(";") || !need("return") ||
        !need("match") || !need("<") || !need("int") ||
        !need(">") || !need(*variable) || !need("over") ||
        !need("on") || !parse_pattern()) return out_;
    if (eat("when")) {
      const auto name=peek();
      const auto index=captured(name);
      if (!index) { fail(Error::binding); return out_; }
      ++at_;
      const auto op=peek();
      if (op!=">" && op!=">=" && op!="<" && op!="<=" &&
          op!="==" && op!="!=") { fail(Error::unsupported); return out_; }
      ++at_;
      const auto value=number();
      if (!value) return out_;
      out_.program.guard=Guard{*index,std::string(op),*value};
    }
    if (!need("->") || !parse_arm_result() || !need(",") ||
        !need("on") || !need("_") || !need("->")) return out_;
    if (captured(peek())) { fail(Error::binding); return out_; }
    const auto fallback=number();
    if (!fallback) return out_;
    out_.program.fallback=*fallback;
    if (!need("end") || !need(";") || !need("}") || !need("<eof>"))
      return out_;
    return out_;
  }
};

struct Compiled {
  bool candidate=false;
  Error error=Error::none;
  std::size_t offset=0;
  std::string source;
  [[nodiscard]] bool ok() const noexcept { return candidate && error==Error::none; }
};
[[nodiscard]] inline bool is_candidate(std::string_view source) noexcept {
  return source.find("List<int>")!=std::string_view::npos ||
         source.find("List.of(")!=std::string_view::npos;
}
[[nodiscard]] inline std::string int_text(std::int64_t value) {
  return value==std::numeric_limits<std::int64_t>::min() ?
      "std::numeric_limits<std::int64_t>::min()" : std::to_string(value);
}
[[nodiscard]] inline std::string emit(const Program& p) {
  std::string out="#include <atto/checked_i64.hpp>\n"
      "#include <array>\n#include <cstdint>\n#include <cstdio>\n"
      "#include <optional>\n#include <cstddef>\n#include <limits>\n"
      "int main() {\n";
  out+="  const std::array<atto::forge::i64,"+std::to_string(p.list.size())+"> values{";
  for (std::size_t i=0;i<p.list.size();++i) {
    if (i) out+=",";
    if (p.list[i]==std::numeric_limits<std::int64_t>::min()) out+="std::numeric_limits<std::int64_t>::min()";
    else out+=std::to_string(p.list[i]);
  }
  out+="};\n";
  const auto fixed=p.fixed.size(), n=p.list.size();
  const auto fallback=[&]() {
    out+="  std::printf(\"%lld\\n\",static_cast<long long>("+int_text(p.fallback)+"));\n"
         "  return 0;\n";
  };
  if (n>=fixed) {
    out+="  for (std::size_t start=0;start<="+std::to_string(n-fixed)+";++start) {\n";
    if (!p.leading) out+="    if(start!=0) continue;\n";
    if (!p.trailing) out+="    if(start+"+std::to_string(fixed)+"!=values.size()) continue;\n";
    out+="    if (";
    bool first=true;
    for (std::size_t i=0;i<fixed;++i) {
      const auto& atom=p.fixed[i];
      if (atom.kind!=AtomKind::literal) continue;
      if (!first) out+=" && ";
      first=false;
      out+="values[start+"+std::to_string(i)+"]=="+int_text(atom.value);
    }
    if (first) out+="true";
    out+=") {\n";
    if (p.guard) {
      out+="      if (values[start+"+std::to_string(p.guard->capture)+"]"+
          p.guard->comparison+int_text(p.guard->value)+") {\n";
    }
    const auto &terms=p.result;
    if (!terms.empty()) {
      const auto operand=[&](const Term& t) {
        return t.capture ? "values[start+"+std::to_string(*t.capture)+"]" :
               int_text(t.value);
      };
      out+="      auto total=atto::forge::i64{"+operand(terms[0])+"};\n";
      for (std::size_t i=1;i<terms.size();++i) {
        const auto temp="sum"+std::to_string(i);
        out+="      auto "+temp+"=atto::forge::checked_add(total,"+operand(terms[i])+
             ");\n      if(!"+temp+"){std::fputs(\"E_OVERFLOW\\n\",stderr);return 70;}\n"
             "      total=*"+temp+";\n";
      }
      out+="      std::printf(\"%lld\\n\",static_cast<long long>(total));\n"
           "      return 0;\n";
    }
    if (p.guard) out+="      }\n";
    // Do NOT retry an alternative decomposition if the guard failed:
    // the pattern's first matching witness owns the sole guard attempt.
    out+="      std::printf(\"%lld\\n\",static_cast<long long>("+
         int_text(p.fallback)+"));\n      return 0;\n    }\n";
    out+="  }\n";
  }
  fallback();
  out+="}\n";
  return out;
}
[[nodiscard]] inline Compiled compile(std::string_view source) {
  Compiled result;
  result.candidate=is_candidate(source);
  if (!result.candidate) return result;
  const auto lexed=lex(source);
  if (lexed.error!=Error::none) {
    result.error=lexed.error;result.offset=lexed.offset;return result;
  }
  const auto parsed=Parser(lexed.tokens).parse();
  result.error=parsed.error;
  result.offset=parsed.offset;
  if (!parsed.ok()) return result;
  result.source=emit(parsed.program);
  if (result.source.size()>max_output) {
    result.source.clear();
    result.error=Error::budget;
  }
  return result;
}
} // namespace atto::forge::list_guest
