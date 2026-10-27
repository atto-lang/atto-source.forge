#include "compiler/frontend.hpp"
#include "runtime/generator_frame.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void check(bool ok, const char* why) {
  if (!ok) throw std::runtime_error(why);
}
void rejected(std::string_view source) {
  try {
    (void)oresforge::parse_source(source);
  } catch (const std::runtime_error& ex) {
    check(std::string_view(ex.what()).find("generator declarations require") !=
              std::string_view::npos,
          "wrong generator rejection diagnostic");
    return;
  }
  throw std::runtime_error("generator source unexpectedly accepted");
}
}  // namespace

int main() {
  using oresforge::runtime::GeneratorFrameV1;
  GeneratorFrameV1 frame;
  std::size_t slot = 0;
  check(frame.pull(3, slot) == -1, "fresh pull accepted");
  check(frame.close() == -1, "fresh close accepted");
  check(frame.initialize() == 1 && frame.initialize() == 0, "init not one-shot");
  for (std::size_t expected = 0; expected < 3; ++expected) {
    check(frame.pull(3, slot) == 1 && slot == expected, "yield slot replay/order failure");
  }
  check(frame.pull(3, slot) == 0 && frame.terminal(), "missing terminal DONE");
  check(frame.pull(3, slot) == 0 && frame.close() == 0, "DONE not sticky");
  check(frame.initialize() == 0, "terminal frame resurrected");

  GeneratorFrameV1 early;
  check(early.initialize() == 1 && early.pull(3, slot) == 1 && slot == 0,
        "early close setup failed");
  check(early.close() == 1 && early.terminal(), "active close failed");
  check(early.pull(3, slot) == 0 && early.close() == 0, "closed frame resumed");

  GeneratorFrameV1 bounded;
  check(bounded.initialize() == 1 &&
        bounded.pull(GeneratorFrameV1::kMaxYields + 1, slot) == -1,
        "yield budget not enforced");

  rejected("pub gen* values(): Iterator<int> { yield 1; return; }");
  rejected("pub async gen* values(): AsyncIterator<int> { yield 1; return; }");
  rejected("pub suspend gen* values(): AsyncIterator<int> { yield 1; return; }");

  std::cout << "FORGE_GENERATOR_FRAME_V1_PASS\n";
  return EXIT_SUCCESS;
}
