#include <cstdint>
#include <iostream>
extern "C" std::int64_t oresforge_entry();
int main() {
  const auto observed=oresforge_entry();
  std::cout << "ATTO_FORGE_NATIVE_GUEST=" << observed << '\n';
  return observed==42 || observed==43 ? 0 : 2;
}
