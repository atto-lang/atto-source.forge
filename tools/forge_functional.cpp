// Bounded Atto functional-pipeline frontend; real checked native C++ guests.
// Existing forge_scalar (single-expression frontend) remains unchanged.
// All C++ aggregate nodes initialize every member explicitly under -Werror.
// No Python bootstrap, host-side guest evaluation, or raw guest-source output.
#include <atto/checked_i64.hpp>
#include <algorithm>
#include <charconv>
#include <cerrno>
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
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
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
  std::vector<Expr> prints;
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
    } else if (std::string_view{"(){}:.,;+-*/="}.find(c) != std::string_view::npos) {
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
  bool allow_stdio;
  explicit Parser(std::string_view s, bool grant) : tokens(tokenize(s)), allow_stdio(grant) {}
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
    // A scalar bootstrap must not turn authority/effect keywords into
    // ordinary callable symbols, even if it does not implement those effects.
    // Keep this synchronized with atto-spec and the JS/Cranelift subsets.
    static const std::set<std::string> reserved = {
      "pub","fnc","int","return","val","let","true","false","bool",
      "actor","unsafe","await","async","susp","suspends","critical",
      "trap","panic","raise","throw","borrow","take","share","copy",
      "drop","pin","rt","io","stdio","process","spawn","select",
      "import","module","define","namespace","use","pipe","rpipe",
      "sync","task","yield","constructor","prototype","__proto__"};
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
      // -2^63 is a valid signed i64 literal, although its unsigned
      // magnitude (2^63) does not fit in i64. Construct the MIN value
      // directly rather than parsing +2^63 or using a UB-prone C++ negation.
      if (peek() == "9223372036854775808") {
        ++at;
        lhs = Expr{Expr::Kind::lit, std::numeric_limits<i64>::min(), "", {}};
      } else {
        Expr rhs = expr(30, depth + 1);
        lhs = Expr{Expr::Kind::sub, 0, "",
                   {Expr{Expr::Kind::lit, 0, "", {}}, std::move(rhs)}};
      }
    } else if (!peek().empty() && digit(peek().front())) {
      i64 value = 0;
      const auto& digits = peek();
      const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
      if (error != std::errc{} || end != digits.data() + digits.size()) fail("E_LITERAL");
      ++at;
      lhs = Expr{Expr::Kind::lit, value, "", {}};
    } else if (peek() == "true" || peek() == "false") {
      fail("E_TYPE");
    } else {
      std::string symbol = name();
      if (peek() == "(")
        lhs = Expr{Expr::Kind::call, 0, std::move(symbol), arguments(depth)};
      else lhs = Expr{Expr::Kind::ref, 0, std::move(symbol), {}};
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
  Expr brace(std::vector<Binding>& locals, std::vector<Expr>& prints, const std::string& function_name) {
    while (eat("val")) {
      auto n = name();
      need("=");
      auto v = expr(0, 0);
      need(";");
      if (locals.size() >= 4) fail("E_LIMIT");
      locals.push_back(Binding{std::move(n), std::move(v)});
    }
    while (eat("stdio")) {
      if (!allow_stdio || function_name != "main") fail("E_PERMISSION");
      need(".");
      if (!eat("println")) fail("E_STDIO");
      need("(");
      auto value = expr(0, 0);
      need(")");
      need(";");
      if (prints.size() >= 4) fail("E_LIMIT");
      prints.push_back(std::move(value));
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
      std::vector<Expr> prints;
      Expr answer;
      if (eat("{")) answer = brace(locals, prints, n);
      else if (colon && eat("->")) {
        if (eat("{")) answer = brace(locals, prints, n);
        else { answer = expr(0, 0); need(";"); }
      } else fail("E_PARSE");
      functions.push_back(Function{std::move(n), std::move(params),
                                   std::move(locals), std::move(prints), std::move(answer)});
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
      if (!functions[static_cast<std::size_t>(target)].prints.empty()) fail("E_EFFECT_CALL");
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
  for (const auto& value : fn.prints) check_expr(value, functions, state, available);
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
        // C++ lexical integer literals cannot spell signed i64::MIN with
        // a positive decimal magnitude and a later unary minus; emit it
        // from two representable operands instead, with no overflow.
        line("const atto::forge::i64 " + id + " = " +
             (e.number == std::numeric_limits<i64>::min()
               ? std::string{"(-9223372036854775807LL - 1LL)"}
               : std::to_string(e.number) + "LL") + ";");
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
    for (const auto& print : fn.prints) {
      auto value = e.expression(print, fn);
      e.line("if (std::printf(\"%lld\\n\", static_cast<long long>(" + value + ")) < 0) return std::nullopt;");
    }
    auto result = e.expression(fn.result, fn);
    e.line("return " + result + ";");
    e.out += "}\n";
    if (e.out.size() > 65536) fail("E_EMIT_LIMIT");
  }
  const bool effectful = !functions.at(static_cast<std::size_t>(find_function(functions, "main"))).prints.empty();
  e.out += "int main() {\n"
           "  const auto value = f_main();\n";
  if (effectful) {
    e.out += "  if (std::fflush(stdout) != 0 || std::ferror(stdout)) { std::fputs(\"E_IO\\n\", stderr); return 71; }\n"
             "  if (!value) { std::fputs(\"E_OVERFLOW\\n\", stderr); return 70; }\n"
             "  if (*value < 0 || *value > 255) { std::fputs(\"E_EXIT_RANGE\\n\", stderr); return 72; }\n"
             "  return static_cast<int>(*value);\n}\n";
  } else {
    e.out += "  if (!value) { std::fputs(\"E_OVERFLOW\\n\", stderr); return 70; }\n"
             "  std::printf(\"%lld\\n\", static_cast<long long>(*value));\n"
             "  return 0;\n}\n";
  }
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
  // A filename-only -o destination is relative to the admitted current
  // working directory; it must not be rejected solely for lacking '/'.
  if (parent.empty()) parent = ".";
  if (!fs::is_directory(parent, ec) || ec || fs::is_symlink(parent, ec) || ec)
    fail("E_OUTPUT");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    const bool allow_stdio = argc > 1 && std::string_view(argv[1]) == "--allow-stdio";
    if (allow_stdio) { --argc; ++argv; }
    if (argc != 3 && !(argc == 4 && std::string(argv[2]) == "-o"))
      fail("E_USAGE");
    const bool check_only = argc == 3 && std::string(argv[1]) == "--check";
    if (argc == 3 && !check_only) fail("E_USAGE");
    fs::path input = check_only ? fs::path(argv[2]) : fs::path(argv[1]);
    if (input.extension() != ".atto") fail("E_PATH");
    std::error_code ec;
    std::string source;
#if defined(__linux__) || defined(__APPLE__)
    // Validate and read the SAME opened inode. A preliminary path-based
    // is_regular_file/is_symlink check followed by ifstream is racy: a
    // competing process could replace the source with a symlink between
    // those checks and the second open, including an out-of-scope host file.
    // O_NONBLOCK prevents blocking on an attacker-substituted FIFO.
    const int source_fd = ::open(input.c_str(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (source_fd < 0) fail("E_SOURCE");
    struct stat stat_before {};
    if (::fstat(source_fd, &stat_before) != 0 ||
        !S_ISREG(stat_before.st_mode)) {
      ::close(source_fd);
      fail("E_SOURCE");
    }
    if (stat_before.st_size < 0 || stat_before.st_size > 16384) {
      ::close(source_fd);
      fail("E_SOURCE_LIMIT");
    }
    source.resize(static_cast<std::size_t>(stat_before.st_size));
    std::size_t read_bytes = 0;
    bool source_ok = true;
    while (read_bytes < source.size()) {
      const ssize_t n = ::read(source_fd, source.data() + read_bytes,
                               source.size() - read_bytes);
      if (n > 0) read_bytes += static_cast<std::size_t>(n);
      else if (n < 0 && errno == EINTR) continue;
      else { source_ok = false; break; }
    }
    // Even if another writer extended the file after fstat, refuse a
    // truncated/mixed snapshot rather than silently compiling a prefix.
    char trailing{};
    ssize_t extra{};
    do { extra = ::read(source_fd, &trailing, 1); }
    while (extra < 0 && errno == EINTR);
    if (extra != 0) source_ok = false;
    const bool source_closed = ::close(source_fd) == 0;
    if (!source_ok || !source_closed) fail("E_SOURCE");
#else
    fail("E_SOURCE_UNSUPPORTED");
#endif
    auto functions = Parser(source, allow_stdio).parse();
    validate(functions);
    if (check_only) return 0;
    fs::path output(argv[3]);
    safe_target(input, output);
    auto generated = emit(functions);
    // The output is a newly allocated, independent guest artifact. Never
    // delete or replace an existing destination, including on cleanup errors.
    const fs::path parent = output.parent_path().empty()
        ? fs::path{"."} : output.parent_path();
    const fs::path tmp = fs::path(output.string() + ".part");
    ec.clear();
    const auto tmp_status = fs::symlink_status(tmp, ec);
    if (ec != std::errc::no_such_file_or_directory &&
        (ec || fs::exists(tmp_status) || fs::is_symlink(tmp_status)))
      fail("E_OUTPUT");
#if defined(__linux__) && defined(O_TMPFILE) && defined(AT_EMPTY_PATH)
    // Linux: the generated source is initially an UNNAMED inode. The
    // output directory is held open through the atomic no-replace link,
    // so a competing process cannot substitute a symlink for a .part
    // pathname or redirect us to a newly replaced parent directory.
    // Unsupported filesystems fail closed rather than falling back to a
    // path-based publication vulnerable to shared-directory races.
    const int dirfd = ::open(parent.c_str(),
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dirfd < 0) fail("E_OUTPUT");
    const int fd = ::openat(dirfd, ".",
                            O_TMPFILE | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) { ::close(dirfd); fail("E_OUTPUT"); }
    std::size_t written = 0;
    bool valid = true;
    while (written < generated.size()) {
      const ssize_t count = ::write(fd, generated.data() + written,
                                    generated.size() - written);
      if (count > 0) written += static_cast<std::size_t>(count);
      else if (count < 0 && errno == EINTR) continue;
      else { valid = false; break; }
    }
    if (valid && ::fsync(fd) != 0) valid = false;
    if (!valid) {
      ::close(fd);
      ::close(dirfd);
      fail("E_OUTPUT");
    }
    // Link the EXACT inode we just wrote, directly from its still-open
    // descriptor. AT_EMPTY_PATH prevents pathname substitution; linkat
    // fails with EEXIST if another publisher claimed the output first.
    const bool linked = ::linkat(fd, "", dirfd,
                                 output.filename().c_str(),
                                 AT_EMPTY_PATH) == 0;
    const bool closed_file = ::close(fd) == 0;
    const bool closed_dir = ::close(dirfd) == 0;
    if (!linked) fail("E_OUTPUT");
    // Never roll back an output by its pathname: another process could
    // replace the directory entry between linking and our cleanup.
    if (!closed_file || !closed_dir) fail("E_OUTPUT_CLEANUP");
#elif defined(__APPLE__)
    // The macOS bootstrap still uses exclusive named-temp creation.
    // Use only in a trusted build directory; Apple lacks Linux O_TMPFILE
    // and this mode is NOT certified for hostile shared output folders.
    const int fd = ::open(tmp.c_str(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                          0600);
    if (fd < 0) fail("E_OUTPUT");
    std::size_t written = 0;
    bool valid = true;
    while (written < generated.size()) {
      const ssize_t count = ::write(fd, generated.data() + written,
                                    generated.size() - written);
      if (count > 0) written += static_cast<std::size_t>(count);
      else if (count < 0 && errno == EINTR) continue;
      else { valid = false; break; }
    }
    if (::close(fd) != 0) valid = false;
    if (!valid) { fs::remove(tmp, ec); fail("E_OUTPUT"); }
    ec.clear();
    fs::create_hard_link(tmp, output, ec);
    if (ec) { ec.clear(); fs::remove(tmp, ec); fail("E_OUTPUT"); }
    ec.clear();
    fs::remove(tmp, ec);
    if (ec) fail("E_OUTPUT_CLEANUP"); // Never delete a published output
#else
    fail("E_OUTPUT_UNSUPPORTED");
#endif
    return 0;
  } catch (const Reject& e) {
    std::cerr << e.code << '\n';
    return 2;
  }
}
