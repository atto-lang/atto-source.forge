#include "compiler/frontend.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void accept(std::string_view source) {
  try {(void)oresforge::parse_source(source);}
  catch(const std::exception& e) {
    throw std::runtime_error(std::string("scalar baseline regression: ")+e.what());
  }
}
void reject(std::string_view source, std::string_view diagnostic) {
  try {(void)oresforge::parse_source(source);}
  catch(const std::exception& e) {
    const std::string message(e.what());
    if(message.find(diagnostic)!=std::string::npos &&
       message.find("at byte ")!=std::string::npos) return;
    throw std::runtime_error("unexpected parser diagnostic: "+message);
  }
  throw std::runtime_error("Forge admitted unsupported String expression");
}
}

int main() {
  accept("define pub fnc main():int{return 42;}");
  accept("define pub fnc main():int{/* '\"${ */return 42;}");
  reject("define pub fnc main():int{return 'single';}", "STRING_BACKEND_UNSUPPORTED");
  reject("define pub fnc main():int{return \"double\";}", "STRING_BACKEND_UNSUPPORTED");
  reject("define pub fnc unused():int{return 'hidden';}"
         " define pub fnc main():int{return 42;}", "STRING_BACKEND_UNSUPPORTED");
  const char tick=static_cast<char>(96);
  const std::string valid="define pub fnc main():int{return "+std::string(1,tick)+
      "x ${'quoted } braces'}"+tick+";}";
  reject(valid, "STRING_BACKEND_UNSUPPORTED");
  reject("define pub fnc main():int{return 'broken;}", "unterminated quoted string");
  reject("define pub fnc main():int{return 'bad\\q';}", "invalid source literal escape");
  const std::string empty="define pub fnc main():int{return "+
      std::string(1,tick)+"x ${}"+tick+";}";
  reject(empty, "empty template interpolation");
  return 0;
}
