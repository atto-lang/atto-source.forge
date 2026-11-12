#include "compiler/frontend.hpp"
#include "compiler/quoted_source_syntax.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void accept_lex(std::string_view literal) {
  const auto end=oresforge::source_syntax::scan_quoted(literal,0);
  if(end!=literal.size()) throw std::runtime_error("incomplete quoted literal");
}
void accept_guest(std::string_view program) {
  try { (void)oresforge::parse_source(program); }
  catch(const std::exception& e) {
    throw std::runtime_error(std::string("valid guest rejected: ")+e.what());
  }
}
void reject_lex(std::string_view literal,std::string_view needle) {
  try { (void)oresforge::source_syntax::scan_quoted(literal,0); }
  catch(const std::exception& e) {
    const std::string diagnostic=e.what();
    if(diagnostic.find(needle)!=std::string::npos &&
       diagnostic.find("at byte ")!=std::string::npos) return;
    throw std::runtime_error("incorrect lexical diagnostic: "+diagnostic);
  }
  throw std::runtime_error("malformed string accepted");
}
void reject_guest(std::string_view program,std::string_view needle) {
  try { (void)oresforge::parse_source(program); }
  catch(const std::exception& e) {
    const std::string diagnostic=e.what();
    if(diagnostic.find(needle)!=std::string::npos &&
       diagnostic.find("at byte ")!=std::string::npos) return;
    throw std::runtime_error("incorrect compiler diagnostic: "+diagnostic);
  }
  throw std::runtime_error("unsupported String guest accepted");
}
}
int main() {
  try {
    for(const auto& s : std::vector<std::string> {
      R"atto('single')atto",
      R"atto("double")atto",
      R"atto('opposite "quote"')atto",
      R"atto("opposite 'quote'")atto",
      R"atto('escape \' quote')atto",
      R"atto("escape \" quote")atto",
      R"atto('unicode \u0041')atto",
      R"atto("hex \x41")atto",
      "`raw\nmultiline`",
      R"atto(`escaped \${not_interpolation}`)atto",
      R"atto(`outer ${call({a: "}", b: 1}, /* { */ `inner ${value}`)} end`)atto",
      R"atto(`outer ${fnc({a: "}"} /* nested /* comment */ comment */)} end`)atto"
    }) accept_lex(s);
    for(const auto& [bad,diagnostic] : std::vector<std::pair<std::string,std::string>> {
      {"'raw\nnewline'","newline in quoted string"},
      {R"atto('unclosed)atto","unterminated string/template"},
      {R"atto('\q')atto","invalid string escape"},
      {R"atto('\u12X4')atto","invalid unicode escape"},
      {R"atto('\x0G')atto","invalid hex escape"},
      {R"atto(`bad ${ }`)atto","empty template interpolation"},
      {R"atto(`bad ${call(1)`)atto","unterminated string/template"},
      {R"atto(`bad ${/* unclosed}`)atto","unterminated interpolation comment"},
      {R"atto(`bad ${'unclosed}`)atto","unterminated string/template"}
    }) reject_lex(bad,diagnostic);
    accept_guest("pub fnc main(): int { /* 'template ${safe}' */ return 42; }");
    accept_guest("pub fnc main(): int { // `template ${safe}`\n return 42; }");
    reject_guest(R"atto(pub fnc main(): int { return 'string'; })atto",
                 "STRING_BACKEND_UNSUPPORTED");
    reject_guest(R"atto(pub fnc main(): int { return "string"; })atto",
                 "STRING_BACKEND_UNSUPPORTED");
    reject_guest(R"atto(pub fnc main(): int { return `value ${x}`; })atto",
                 "STRING_BACKEND_UNSUPPORTED");
    reject_guest(R"atto(pub fnc helper(): int { return `unreachable`; }
                       pub fnc main(): int { return 42; })atto",
                 "STRING_BACKEND_UNSUPPORTED");
    std::cout<<"FORGE_QUOTED_TEMPLATE_GRAMMAR_PASS positive=12 "
                "negative=9 guest_positive=2 guest_negative=4\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"FORGE_QUOTED_TEMPLATE_GRAMMAR_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
