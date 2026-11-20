// Bounded Atto functional-pipeline frontend; real checked native C++ guests.
// Existing forge_scalar (single-expression frontend) remains unchanged.
// All C++ aggregate nodes initialize every member explicitly under -Werror.
// No Python bootstrap, host-side guest evaluation, or raw guest-source output.
#include <atto/checked_i64.hpp>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
namespace fs = std::filesystem;
using i64 = atto::forge::i64;
namespace {
struct Reject { std::string code; };
[[noreturn]] void fail(const char* code) { throw Reject{code}; }
struct Expr {
  enum class Kind { lit, ref, add, sub, mul, call } kind;
  i64 number = 0;
  std::string name;
  std::vector<Expr> args;
};
struct Binding { std::string name; Expr value; };
struct Function {
  std::string name;
  std::vector<std::string> params;
  std::vector<Binding> locals;
  Expr result;
};
bool ascii_head(char c) noexcept {
  return (c >= 'a' && c <= 'z') || c == '_';
}
bool ascii_tail(char c) noexcept {
  return ascii_head(c) || (c >= '0' && c <= '9');
}
bool digit(char c) noexcept { return c >= '0' && c <= '9'; }
std::vector<std::string> tokenize(std::string_view text) {
  if (text.size() > 16384) fail("E_SOURCE_LIMIT");
  std::vector<std::string> tokens;
  std::size_t at = 0;
  while (at < text.size()) {
    if (text[at] == ' ' || text[at] == '\r' ||
        text[at] == '\n' || text[at] == '\t') { ++at; continue; }
    if (text.substr(at, 2) == "//") {
      at += 2;
      while (at < text.size() && text[at] != '\n') ++at;
      continue;
    }
    if (text.substr(at, 2) == "/*") {
      at += 2;
      auto end = text.find("*/", at);
      if (end == std::string_view::npos) fail("E_LEX");
      at = end + 2;
      continue;
    }
    if (tokens.size() >= 512) fail("E_TOKEN_LIMIT");
    std::size_t start = at;
    char c = text[at];
    if (ascii_head(c)) {
      ++at;
      while (at < text.size() && ascii_tail(text[at])) ++at;
    } else if (digit(c)) {
      ++at;
      while (at < text.size() && digit(text[at])) ++at;
    } else if (text.substr(at, 2) == "|>" ||
               text.substr(at, 2) == "<|" ||
               text.substr(at, 2) == "->") {
      at += 2;
    } else if (std::string_view{"(){}:,;+-*/="}.find(c) != std::string_view::npos) {
      ++at;
    } else fail("E_LEX");
    tokens.emplace_back(text.substr(start, at - start));
  }
  tokens.emplace_back("<eof>");
  return tokens;
}
struct Parser {
  std::vector<std::string> tokens;
  std::size_t at = 0;
  explicit Parser(std::string_view s) : tokens(tokenize(s)) {}
  const std::string& peek() const { return tokens.at(at); }
  bool eat(std::string_view tok) {
    if (peek() != tok) return false;
    ++at;
    return true;
  }
  void need(std::string_view tok) { if (!eat(tok)) fail("E_PARSE"); }
  std::string name() {
    if (peek().empty() || !ascii_head(peek().front())) fail("E_NAME");
    const std::string result = peek();
    for (char c : result) if (!ascii_tail(c)) fail("E_NAME");
    static const std::set<std::string> reserved = {
      "pub","fnc","int","return","val","let","true","false","bool"};
    if (reserved.contains(result)) fail("E_NAME");
    ++at;
    return result;
  }
  std::vector<Expr> arguments(int depth) {
    need("(");
    std::vector<Expr> args;
    if (peek() != ")") {
      do {
        args.push_back(expr(0, depth + 1));
        if (args.size() > 8) fail("E_LIMIT");
      } while (eat(","));
    }
    need(")");
    return args;
  }
  Expr expr(int min, int depth, const std::string& direction = "") {
    if (depth > 24) fail("E_DEPTH");
    Expr lhs;
    if (eat("(")) {
      lhs = expr(0, depth + 1);
      need(")");
    } else if (eat("-")) {
      Expr rhs = expr(30, depth + 1);
      lhs = Expr{Expr::Kind::sub, 0, "", {Expr{Expr::Kind::lit, 0, "", {}}, std::move(rhs)}};
    } else if (!peek().empty() && digit(peek().front())) {
      i64 value = 0;
      const auto& digits = peek();
      const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
      if (error != std::errc{} || end != digits.data() + digits.size()) fail("E_LITERAL");
      ++at;
      lhs = Expr{Expr::Kind::lit, value, "", {}};
    } else {
      std::string symbol = name();
      if (peek() == "(")
        lhs = Expr{Expr::Kind::call, 0, std::move(symbol), arguments(depth)};
      else lhs = Expr{Expr::Kind::ref, 0, std::move(symbol)};
    }
    std::string current_direction = direction;
    while (true) {
      const auto op = peek();
      int precedence = (op == "*" || op == "/") ? 20 :
                       (op == "+" || op == "-") ? 10 :
                       (op == "|>" || op == "<|") ? 5 : -1;
      if (precedence < min) break;
      ++at;
      if (op == "|>" || op == "<|") {
        if (!current_direction.empty() && current_direction != op) fail("E_MIXED_PIPE");
        current_direction = op;
        if (op == "|>") {
          auto target = name();
          auto args = peek() == "(" ? arguments(depth + 1) : std::vector<Expr>{};
          if (args.size() >= 8) fail("E_ARITY");
          args.insert(args.begin(), std::move(lhs));
          lhs = Expr{Expr::Kind::call, 0, std::move(target), std::move(args)};
        } else {
          if (lhs.kind != Expr::Kind::ref && lhs.kind != Expr::Kind::call)
            fail("E_CALLEE");
          auto target = lhs.name;
          auto args = std::move(lhs.args);
          if (args.size() >= 8) fail("E_ARITY");
          args.insert(args.begin(), expr(precedence, depth + 1, current_direction));
          lhs = Expr{Expr::Kind::call, 0, std::move(target), std::move(args)};
        }
      } else {
        if (op == "/") fail("E_UNSUPPORTED");
        Expr rhs = expr(precedence + 1, depth + 1, current_direction);
        lhs = Expr{op == "*" ? Expr::Kind::mul :
                   op == "+" ? Expr::Kind::add : Expr::Kind::sub, 0,
                   "", {std::move(lhs), std::move(rhs)}};
      }
    }
    return lhs;
  }
  Expr brace(std::vector<Binding>& locals) {
    while (eat("val")) {
      auto n = name();
      need("=");
      auto v = expr(0, 0);
      need(";");
      if (locals.size() >= 4) fail("E_LIMIT");
      locals.push_back(Binding{std::move(n), std::move(v)});
    }
    need("return");
    Expr answer = expr(0, 0);
    need(";");
    need("}");
    return answer;
  }
  std::vector<Function> parse() {
    std::vector<Function> functions;
    while (peek() != "<eof>") {
      (void)eat("pub");
      need("fnc");
      auto n = name();
      need("(");
      std::vector<std::string> params;
      if (peek() != ")") do {
        need("int");
        auto p = name();
        if (std::find(params.begin(), params.end(), p) != params.end())
          fail("E_DUP_PARAM");
        params.push_back(std::move(p));
        if (params.size() > 8) fail("E_LIMIT");
      } while (eat(","));
      need(")");
      bool colon = eat(":");
      if (!colon) need("->");
      need("int");
      std::vector<Binding> locals;
      Expr answer;
      if (eat("{")) answer = brace(locals);
      else if (colon && eat("->")) {
        if (eat("{")) answer = brace(locals);
        else { answer = expr(0, 0); need(";"); }
      } else fail("E_PARSE");
      functions.push_back(Function{std::move(n), std::move(params),
                                   std::move(locals), std::move(answer)});
      if (functions.size() > 16) fail("E_LIMIT");
    }
    return functions;
  }
};
int find_function(const std::vector<Function>& functions, std::string_view name) {
  for (std::size_t i = 0; i < functions.size(); ++i)
    if (functions[i].name == name) return static_cast<int>(i);
  return -1;
}
void check_fn(std::size_t index, const std::vector<Function>& functions,
              std::vector<int>& state);
void check_expr(const Expr& e, const std::vector<Function>& functions,
                std::vector<int>& state, const std::set<std::string>& available) {
  switch (e.kind) {
    case Expr::Kind::lit: return;
    case Expr::Kind::ref:
      if (!available.contains(e.name)) fail("E_SYMBOL");
      return;
    case Expr::Kind::call: {
      auto target = find_function(functions, e.name);
      if (target < 0) fail("E_SYMBOL");
      if (functions[static_cast<std::size_t>(target)].params.size() != e.args.size()) fail("E_ARITY");
      for (const auto& arg : e.args) check_expr(arg, functions, state, available);
      check_fn(static_cast<std::size_t>(target), functions, state);
      return;
    }
    case Expr::Kind::add:
    case Expr::Kind::sub:
    case Expr::Kind::mul:
      if (e.args.size() != 2) fail("E_AST");
      for (const auto& arg : e.args) check_expr(arg, functions, state, available);
      return;
  }
  fail("E_AST");
}
void check_fn(std::size_t index, const std::vector<Function>& functions,
              std::vector<int>& state) {
  if (state[index] == 2) return;
  if (state[index] == 1) fail("E_CYCLE");
  state[index] = 1;
  const auto& fn = functions[index];
  std::set<std::string> available(fn.params.begin(), fn.params.end());
  for (const auto& b : fn.locals) {
    check_expr(b.value, functions, state, available);
    if (!available.insert(b.name).second) fail("E_DUP_LOCAL");
  }
  check_expr(fn.result, functions, state, available);
  state[index] = 2;
}
void validate(const std::vector<Function>& functions) {
  auto main = find_function(functions, "main");
  if (main < 0 || !functions[static_cast<std::size_t>(main)].params.empty()) fail("E_ENTRY");
  std::set<std::string> unique;
  for (const auto& fn : functions)
    if (!unique.insert(fn.name).second) fail("E_DUP_FUNCTION");
  std::vector<int> state(functions.size(), 0);
  for (std::size_t i = 0; i < functions.size(); ++i) check_fn(i, functions, state);
}
struct Emitter {
  std::string out;
  std::size_t next = 0;
  void line(std::string s) {
    out += "  " + s + "\n";
    if (out.size() > 65536) fail("E_EMIT_LIMIT");
  }
  std::string expression(const Expr& e, const Function& fn) {
    const std::string id = "t" + std::to_string(next++);
    switch (e.kind) {
      case Expr::Kind::lit:
        line("const atto::forge::i64 " + id + " = " + std::to_string(e.number) + "LL;");
        return id;
      case Expr::Kind::ref: {
        const bool param = std::find(fn.params.begin(), fn.params.end(), e.name) != fn.params.end();
        line("const atto::forge::i64 " + id + " = " +
             (param ? "p_" : "v_") + e.name + ";");
        return id;
      }
      case Expr::Kind::add:
      case Expr::Kind::sub:
      case Expr::Kind::mul: {
        auto a = expression(e.args.at(0), fn);
        auto b = expression(e.args.at(1), fn);
        auto checked = e.kind == Expr::Kind::add ? "checked_add" :
                       e.kind == Expr::Kind::sub ? "checked_sub" : "checked_mul";
        line("const auto c_" + id + " = atto::forge::" + checked + "(" + a + ", " + b + ");");
        line("if (!c_" + id + ") return std::nullopt;");
        line("const atto::forge::i64 " + id + " = *c_" + id + ";");
        return id;
      }
      case Expr::Kind::call: {
        std::vector<std::string> params;
        for (const auto& arg : e.args) params.push_back(expression(arg, fn));
        std::string csv;
        for (const auto& p : params) { if (!csv.empty()) csv += ", "; csv += p; }
        line("const auto c_" + id + " = f_" + e.name + "(" + csv + ");");
        line("if (!c_" + id + ") return std::nullopt;");
        line("const atto::forge::i64 " + id + " = *c_" + id + ";");
        return id;
      }
    }
    fail("E_AST");
  }
};
std::string emit(const std::vector<Function>& functions) {
  Emitter e;
  e.out = "#include <atto/checked_i64.hpp>\n"
          "#include <optional>\n#include <cstdio>\n";
  for (const auto& fn : functions) {
    e.out += "static std::optional<atto::forge::i64> f_" + fn.name + "(";
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
      if (i) e.out += ", ";
      e.out += "atto::forge::i64 p_" + fn.params[i];
    }
    e.out += ");\n";
  }
  for (const auto& fn : functions) {
    e.next = 0;
    e.out += "static std::optional<atto::forge::i64> f_" + fn.name + "(";
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
      if (i) e.out += ", ";
      e.out += "atto::forge::i64 p_" + fn.params[i];
    }
    e.out += ") {\n";
    for (const auto& local : fn.locals) {
      auto value = e.expression(local.value, fn);
      e.line("const atto::forge::i64 v_" + local.name + " = " + value + ";");
    }
    auto result = e.expression(fn.result, fn);
    e.line("return " + result + ";");
    e.out += "}\n";
    if (e.out.size() > 65536) fail("E_EMIT_LIMIT");
  }
  e.out += "int main() {\n"
           "  const auto value = f_main();\n"
           "  if (!value) { std::fputs(\"E_OVERFLOW\\n\", stderr); return 70; }\n"
           "  std::printf(\"%lld\\n\", static_cast<long long>(*value));\n"
           "  return 0;\n}\n";
  if (e.out.size() > 65536) fail("E_EMIT_LIMIT");
  return e.out;
}
void safe_target(const fs::path& source, const fs::path& output) {
  if (source.extension() != ".atto" || output.extension() != ".cpp") fail("E_PATH");
  std::error_code ec;
  if (!fs::is_regular_file(source, ec) || ec || fs::is_symlink(source, ec) || ec) fail("E_PATH");
  ec.clear();
  const auto status = fs::symlink_status(output, ec);
  if (ec != std::errc::no_such_file_or_directory &&
      (ec || fs::exists(status) || fs::is_symlink(status))) fail("E_OUTPUT");
  if (source == output) fail("E_PATH");
  auto parent = output.parent_path();
  if (parent.empty() || !fs::is_directory(parent, ec) || ec || fs::is_symlink(parent, ec) || ec)
    fail("E_OUTPUT");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 3 && !(argc == 4 && std::string(argv[2]) == "-o"))
      fail("E_USAGE");
    const bool check_only = argc == 3 && std::string(argv[1]) == "--check";
    if (argc == 3 && !check_only) fail("E_USAGE");
    fs::path input = check_only ? fs::path(argv[2]) : fs::path(argv[1]);
    if (input.extension() != ".atto") fail("E_PATH");
    std::error_code ec;
    if (!fs::is_regular_file(input, ec) || ec || fs::is_symlink(input, ec) || ec)
      fail("E_SOURCE");
    auto size = fs::file_size(input, ec);
    if (ec || size > 16384) fail("E_SOURCE_LIMIT");
    std::ifstream stream(input, std::ios::binary);
    if (!stream) fail("E_SOURCE");
    std::string source(static_cast<std::size_t>(size), '\0');
    if (!source.empty()) stream.read(source.data(), static_cast<std::streamsize>(size));
    if (!stream || stream.peek() != std::char_traits<char>::eof()) fail("E_SOURCE");
    auto functions = Parser(source).parse();
    validate(functions);
    if (check_only) return 0;
    fs::path output(argv[3]);
    safe_target(input, output);
    auto generated = emit(functions);
    // Refuse output replacement; secure directory-descriptor-relative
    // publication is a separate hardening gate for multi-tenant hosts.
    auto tmp = fs::path(output.string() + ".part");
    ec.clear();
    const auto tmp_status = fs::symlink_status(tmp, ec);
    if (ec != std::errc::no_such_file_or_directory &&
        (ec || fs::exists(tmp_status) || fs::is_symlink(tmp_status))) fail("E_OUTPUT");
    {
      std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
      if (!file) fail("E_OUTPUT");
      file.write(generated.data(), static_cast<std::streamsize>(generated.size()));
      file.flush();
      if (!file) { fs::remove(tmp, ec); fail("E_OUTPUT"); }
    }
    fs::rename(tmp, output, ec);
    if (ec) { fs::remove(tmp, ec); fail("E_OUTPUT"); }
    return 0;
  } catch (const Reject& e) {
    std::cerr << e.code << '\n';
    return 2;
  }
}
