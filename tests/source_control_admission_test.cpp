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
    for (const char* op : {"trap!", "throw!", "raise!", "panic!"}) {
      const auto source = std::string("fnc main():int{return ") + op + " value;}";
      bool rejected = false;
      try { (void)oresforge::parse_source(source); }
      catch (const std::runtime_error& error) {
        if (std::string_view(error.what()).find("EXCEPTION_BANG_BACKEND_UNSUPPORTED")
              == std::string_view::npos) throw;
        rejected = true;
      }
      if (!rejected) throw std::runtime_error("Forge accepted unsupported bang exception");
    }

    // 18 canonical lexical boundary vectors (atto-spec #146). Forge's
    // scalar-only compiler must fail closed on spaced exceptional effects
    // and never misclassify twelve other punctuation/trivia forms.
    for (const char* expr : {
      "trap! parse_id(\"42\")",
      "throw! Error(\"bad\")", "raise! Error(\"bad\")",
      "panic! Error(\"bad\")"
    }) {
      auto source = std::string("fnc main():int{return ") + expr + ";}";
      bool correctly_denied = false;
      try { (void)oresforge::parse_source(source); }
      catch (const std::runtime_error& error) {
        correctly_denied = std::string_view(error.what()).find(
            "EXCEPTION_BANG_BACKEND_UNSUPPORTED") != std::string_view::npos;
      }
      if (!correctly_denied)
        throw std::runtime_error("Forge exceptional-bang escaped denial: " + std::string(expr));
    }
    for (const char* expr : {
      "trap parse_id(\"42\")", "throw Error(\"bad\")",
      "raise Error(\"bad\")", "panic Error(\"bad\")",
      "trap !ready", "trap!=next", "trap!eq next",
      "trapped! parse_id()", "copy! owned"
    }) {
      auto source = std::string("fnc main():int{return ") + expr + ";}";
      try { (void)oresforge::parse_source(source); }
      catch (const std::runtime_error& error) {
        if (std::string_view(error.what()).find("EXCEPTION_BANG_BACKEND_UNSUPPORTED")
            != std::string_view::npos)
          throw std::runtime_error("Forge false exceptional-bang match: " + std::string(expr));
      }
    }
    for (const char* source : {
      "fnc main():int{return 7;} // trap! throw! raise! panic!",
      "fnc main():int{/* trap! throw! raise! panic! */return 7;}",
      "fnc main():int{return \"trap! throw! raise! panic!\";}"
    }) {
      try { (void)oresforge::parse_source(source); }
      catch (const std::runtime_error& error) {
        if (std::string_view(error.what()).find("EXCEPTION_BANG_BACKEND_UNSUPPORTED")
            != std::string_view::npos)
          throw std::runtime_error("Forge reclassified quoted/trivia exceptional keywords");
      }
    }
    for (const char* keyword : {"trap!", "throw!", "raise!", "panic!", "copy!",
        "copy!!", "take!", "move!", "borrow!", "share!", "pin!", "drop!", "pipe!", "rpipe!", "stop!", "expand!"}) {
      for (const char* tail : {"value", "(42)", "/*comment*/ value", ""}) {
        auto source = std::string("fnc main():int{return ") + keyword + tail + ";}";
        bool rejected=false;
        try { (void)oresforge::parse_source(source); }
        catch (const std::runtime_error& e) {
          rejected=std::string_view(e.what()).find("E_BANG_KEYWORD_WHITESPACE")!=std::string_view::npos;
        }
        if (!rejected) throw std::runtime_error("compact keyword not rejected: "+source);
      }
    }
    {
      bool rejected=false;
      try { (void)oresforge::parse_source("fnc main():int{return move! value;}"); }
      catch (const std::runtime_error& e) {
        rejected=std::string_view(e.what()).find("OWNERSHIP_BANG_BACKEND_UNSUPPORTED")!=std::string_view::npos;
      }
      if (!rejected) throw std::runtime_error("move! entered unchecked Forge lowering");
    }
    oresforge::source::validate_utf8("fnc main():int{\r\n\treturn 42;\r\n}");
    std::cout << "source terminal-control admission passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
