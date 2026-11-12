#include "compiler/frontend.hpp"
#include "compiler/quoted_source_syntax.hpp"
#include <cstdint>
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
    std::size_t positives=0, negatives=0;
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
      "'raw\nnewline'",
      R"atto('\q')atto",
      R"atto('\x0G')atto",
      R"atto('\uD83D\uDE00')atto",
      R"atto(`escaped \${not_interpolation}`)atto",
      R"atto(`outer ${call({a: "}", b: 1}, /* { */ `inner ${value}`)} end`)atto",
      R"atto(`outer ${fnc({a: "}"} /* nested /* comment */ comment */)} end`)atto"
    }) { accept_lex(s); ++positives; }
    for(const auto& [bad,diagnostic] : std::vector<std::pair<std::string,std::string>> {

      {R"atto('unclosed)atto","unterminated string/template"},

      {R"atto('\u12X4')atto","invalid unicode escape"},
      {R"atto('\uD800')atto","invalid unicode escape"},
      {R"atto('\uDFFF')atto","invalid unicode escape"},
      {R"atto('\u{1f600}')atto","invalid unicode escape"},
      {R"atto(`invalid \q`)atto","invalid template escape"},
      {R"atto(`invalid \x41`)atto","invalid template escape"},
      {R"atto(`invalid \u{1f600}`)atto","invalid unicode escape"},

      {R"atto(`bad ${ }`)atto","empty template interpolation"},
      {R"atto(`bad ${/* only comment */}`)atto","empty template interpolation"},
      {"`bad ${// only comment\n}`","empty template interpolation"},
      {R"atto(`bad ${call(1)`)atto","unterminated string/template"},
      {R"atto(`bad ${/* unclosed}`)atto","unterminated interpolation comment"},
      {R"atto(`bad ${'unclosed}`)atto","unterminated string/template"}
    }) { reject_lex(bad,diagnostic); ++negatives; }
    const char tick=static_cast<char>(96);
    auto many=[&](int n) {
      std::string out(1,tick);
      for(int i=0;i<n;++i) out+="${'x'}";
      out+=tick;
      return out;
    };
    auto braces=[&](int n) {
      return std::string(1,tick)+"${"+std::string(n,'{')+"x"+
             std::string(n,'}')+"}"+std::string(1,tick);
    };
    auto nested=[&](int n) {
      std::string out(1,tick);
      for(int i=0;i<n;++i) {out+="${";out+=tick;}
      out+="x";
      for(int i=0;i<n;++i) {out+=tick;out+="}";}
      out+=tick;
      return out;
    };
    accept_lex(many(1024));++positives;
    reject_lex(many(1025),"template substitution limit");++negatives;
    accept_lex(braces(255));++positives;
    reject_lex(braces(256),"interpolation brace limit");++negatives;
    accept_lex(nested(16));++positives;
    reject_lex(nested(17),"nesting limit");++negatives;
    std::uint32_t state=0xc01d1234u;
    constexpr std::string_view alphabet = "abc01{}$/*'\"`\\\\ \\n\\rux()";
    for(std::size_t k=0;k<24000;++k) {
      state=state*1664525u+1013904223u;
      std::string source(1,static_cast<char>(k%3==0?39:k%3==1?34:96));
      const auto len=state%120u;
      for(std::uint32_t j=0;j<len;++j) {
        state=state*1664525u+1013904223u;
        source+=alphabet[state%alphabet.size()];
      }
      if((k&1u)==0u) source+=source[0];
      std::size_t end=0;
      try {end=oresforge::source_syntax::scan_quoted(source,0);}
      catch(const std::runtime_error&) {continue;}
      if(end==0 || end>source.size())
        throw std::runtime_error("scanner fuzz span out of range");
    }
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
    std::cout<<"FORGE_QUOTED_TEMPLATE_GRAMMAR_PASS positive="<<positives
             <<" negative="<<negatives
             <<" guest_positive=2 guest_negative=4 fuzz=24000\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"FORGE_QUOTED_TEMPLATE_GRAMMAR_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
