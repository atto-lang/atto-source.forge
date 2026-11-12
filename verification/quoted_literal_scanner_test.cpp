#include "compiler/quoted_literal_scanner.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string("quoted scanner test: ")+std::string(message));
}
void must_reject(const std::string& source) {
  try {(void)oresforge::literals::Scanner(source).scan(0);}
  catch (const std::runtime_error&) {return;}
  throw std::runtime_error("quoted scanner accepted malformed literal");
}
}

int main() {
  using oresforge::literals::Kind;
  using oresforge::literals::Scanner;
  const std::string single = "'say \"hello\"' tail";
  const std::string dbl = "\"it's fine\" tail";
  require(Scanner(single).scan(0).kind == Kind::Quoted, "single type");
  require(Scanner(single).scan(0).end == single.find(" tail"), "single ending");
  require(Scanner(dbl).scan(0).end == dbl.find(" tail"), "double ending");
  const char tick = static_cast<char>(96);
  const std::string nested = std::string(1,tick)+"outer ${"+
    std::string(1,tick)+"inner ${'nested } literal'}"+
    std::string(1,tick)+"}"+std::string(1,tick);
  require(Scanner(nested).scan(0).kind == Kind::Template, "nested kind");
  require(Scanner(nested).scan(0).end == nested.size(), "nested ending");
  const std::string multi = std::string(1,tick)+"line one\nline two ${ /* ignored } */ \"ok\" }"+tick;
  require(Scanner(multi).scan(0).end == multi.size(), "multiline");
  const std::string escaped = "'continued\\\nline'";
  require(Scanner(escaped).scan(0).end == escaped.size(), "line continuation");
  const std::string unicode = std::string(1,tick)+"escape \\u0061"+tick;
  require(Scanner(unicode).scan(0).end == unicode.size(), "Unicode escape");
  must_reject("'broken");
  must_reject("'raw\nnewline'");
  must_reject("'invalid \\q'");
  must_reject("'invalid \\u12G4'");
  must_reject(std::string(1,tick)+"unterminated");
  must_reject(std::string(1,tick)+"literal ${ missing "+tick);
  must_reject(std::string(1,tick)+"x ${ /* open comment "+tick);
  must_reject(std::string(1,tick)+"empty ${}"+tick);
  must_reject(std::string(1,tick)+"comment ${/* skipped */}"+tick);
  must_reject(std::string(1,tick)+"bad \\q"+tick);
  std::string too_many(1,tick);
  for(int i=0;i<1025;++i)too_many+="${'x'}";
  too_many+=tick;
  must_reject(too_many);
  // Verify lexical resource limits at exact positive/negative boundaries.
  auto must_accept = [](const std::string& literal) {
    if (oresforge::literals::Scanner(literal).scan(0).end != literal.size())
      throw std::runtime_error("scanner stopped before literal end");
  };
  auto deep_template = [tick](int depth) {
    std::string v = std::string(1,tick) + "x" + tick;
    for (int i=2;i<=depth;++i) v = std::string(1,tick) + "outer ${" + v + "}" + tick;
    return v;
  };
  must_accept(deep_template(32));
  must_reject(deep_template(33));
  auto nested_comment = [tick](int depth) {
    std::string v = std::string(1,tick)+"${";
    for(int i=0;i<depth;++i) v+="/*";
    v+="ignored";
    for(int i=0;i<depth;++i) v+="*/";
    return v+"name}"+tick;
  };
  must_accept(nested_comment(128));
  must_reject(nested_comment(129));
  auto nested_braces = [tick](int depth) {
    std::string v = std::string(1,tick)+"${";
    v.append(static_cast<std::size_t>(depth),'{');
    v+="name";
    v.append(static_cast<std::size_t>(depth),'}');
    return v+"}"+tick;
  };
  must_accept(nested_braces(256));
  must_reject(nested_braces(257));
  auto substitutions = [tick](int count) {
    std::string v(1,tick);
    for(int i=0;i<count;++i) v+="${\'ok\'}";
    return v+tick;
  };
  must_accept(substitutions(1024));
  must_reject(substitutions(1025));
  return 0;
}
