#include <cstdint>
#include <cstdlib>

namespace {
struct Observer {
  const char* expected = std::getenv("ATTO_TEST_SCOPE_COUNT");
  std::uint64_t tokens[16]{};
  unsigned depth = 0;
  unsigned started = 0;
  unsigned ended = 0;

  ~Observer() {
    const unsigned count = expected ? static_cast<unsigned>(std::atoi(expected)) : 0;
    if (depth || started != count || ended != count) std::abort();
  }
};
Observer observer;
}

extern "C" std::uint64_t ores_native_telemetry_span_begin(const char* name, std::uint64_t length) {
  if (!observer.expected) return 0;
  if (!name || !length || length > 256 || observer.depth == 16) std::abort();
  const auto id = ++observer.started;
  observer.tokens[observer.depth++] = id;
  return id;
}

extern "C" void ores_native_telemetry_span_end(std::uint64_t token) {
  if (token == 0) return;
  if (!observer.depth || observer.tokens[--observer.depth] != token) std::abort();
  ++observer.ended;
}
