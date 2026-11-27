#include "compiler/source_utf8.hpp"
#include "compiler/frontend.hpp"
#include "compiler/call_native.hpp"
#include <stdexcept>
#include <string>
#include <string_view>
#include <iostream>

template<class F> static void expect_denial(F&& parse) {
  try {
    parse();
  } catch (const std::runtime_error& error) {
    if (std::string_view(error.what()).find("terminal control") != std::string_view::npos)
      return;
    throw;
  }
  throw std::runtime_error("terminal-control source admitted");
}
static void denied(const std::string& source) {
  expect_denial([&] { oresforge::source::validate_utf8(source); });
  expect_denial([&] { (void)oresforge::parse_source(source); });
  expect_denial([&] { (void)oresforge::parse_runtime_source(source); });
  expect_denial([&] { (void)oresforge::calls::parse(source); });
}

int main() {
  try {
    for (char control : {'\x1b', '\x7f'}) {
      denied(std::string("fnc main():int{/*") + control + "*/return 42;}");
      denied(std::string("fnc main():int{return 42;}//") + control);
      denied(std::string("fnc main():int{val s = \"") + control + "\";return 42;}");
    }
    // Require the exact lexical denial in both independent frontends.
    for (const char* keyword : {"trap!","throw!","raise!","panic!","copy!","copy!!",
          "take!","move!","borrow!","share!","pin!","drop!","pipe!","rpipe!","stop!","expand!"}) {
      for (const char* tail : {"value","(42)","/*comment*/ value","","\xc2\xa0" "value"}) {
        const auto source=std::string("fnc main():int{return ")+keyword+tail+";}";
        for (int frontend=0;frontend<2;++frontend) {
          bool denied=false;
          try {
            if (frontend==0) (void)oresforge::parse_source(source);
            else (void)oresforge::calls::parse(source);
          } catch (const std::runtime_error& error) {
            denied=std::string_view(error.what()).find("E_BANG_KEYWORD_WHITESPACE")!=std::string_view::npos;
          }
          if (!denied) throw std::runtime_error("compact keyword admitted or misclassified: "+source);
        }
      }
    }
    for (const char* gap : {" ","\t","\r","\n","\v","\f"}) {
      const auto source=std::string("fnc main():int{return move!")+gap+"value;}";
      for (int frontend=0;frontend<2;++frontend) {
        bool denied=false;
        try {
          if (frontend==0) (void)oresforge::parse_source(source);
          else (void)oresforge::calls::parse(source);
        } catch (const std::runtime_error& e) {
          denied=std::string_view(e.what()).find(frontend==0?"OWNERSHIP_BANG_BACKEND_UNSUPPORTED":"BANG_KEYWORD_NATIVE_CALL_UNSUPPORTED")!=std::string_view::npos;
        }
        if (!denied) throw std::runtime_error("spaced transfer lost typed denial");
      }
    }
    (void)oresforge::parse_source("fnc main():int{return 42;} // copy!compact");
    (void)oresforge::calls::parse("fnc main():int{return 42;} // copy!compact");
    oresforge::source::validate_utf8("fnc main():int{\r\n\treturn 42;\r\n}");
    std::cout << "source terminal-control admission passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
