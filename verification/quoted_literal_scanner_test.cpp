#include "compiler/quoted_literal_scanner.hpp"
#include <cassert>
#include <stdexcept>
#include <string>
int main() {
  using oresforge::literals::Kind;
  using oresforge::literals::Scanner;
  const std::string single = "'say \"hello\"' tail";
  const std::string dbl = "\"it's fine\" tail";
  assert(Scanner(single).scan(0).kind == Kind::Quoted);
  assert(Scanner(single).scan(0).end == 13);
  assert(Scanner(dbl).scan(0).end == 11);
  const char tick = static_cast<char>(96);
  const std::string nested = std::string(1,tick)+"outer ${"+
    std::string(1,tick)+"inner ${'nested } literal'}"+
    std::string(1,tick)+"}"+std::string(1,tick);
  assert(Scanner(nested).scan(0).kind == Kind::Template);
  assert(Scanner(nested).scan(0).end == nested.size());
  const std::string multi = std::string(1,tick)+"line one\nline two ${ /* ignored } */ \"ok\" }"+tick;
  assert(Scanner(multi).scan(0).end == multi.size());
  auto must_reject = [](const std::string& source) {
    bool rejected = false;
    try {(void)Scanner(source).scan(0);}
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
  };
  must_reject("'broken");
  must_reject("'raw\nnewline'");
  must_reject(std::string(1,tick)+"unterminated");
  must_reject(std::string(1,tick)+"literal ${ missing "+tick);
  must_reject(std::string(1,tick)+"x ${ /* open comment "+tick);
  return 0;
}
