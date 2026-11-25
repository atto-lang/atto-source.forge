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
    oresforge::source::validate_utf8("fnc main():int{\r\n\treturn 42;\r\n}");
    std::cout << "source terminal-control admission passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
