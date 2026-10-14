#include "compiler/frontend.hpp"
#include "compiler/ir.hpp"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string_view>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,std::size_t size) {
  try {
    auto p=oresforge::parse_source(
      std::string_view(reinterpret_cast<const char*>(data),size));
    oresforge::verify(p);
    (void)oresforge::interpret(p);
  } catch (const std::exception&) {} // Expected invalid input/checked trap.
  return 0;
}
