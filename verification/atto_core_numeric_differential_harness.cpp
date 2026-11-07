// Host-only independent oracle for real compiler-emitted stage-2 numeric
// function code. The checked-in .atto source is not translated into C++;
// only the expected result is computed here. No guest execution is claimed
// until a compiled Forge assembly object is linked and this binary runs.
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <limits>

#if (defined(ATTO_NUMERIC_MIN) + defined(ATTO_NUMERIC_MAX) + defined(ATTO_NUMERIC_CLAMP)) != 1
#error "Choose exactly one Atto numeric native differential entrypoint"
#endif

#if defined(ATTO_NUMERIC_CLAMP)
extern "C" std::int64_t oresforge_entry(std::int64_t value,
                                        std::int64_t lower, std::int64_t upper);
constexpr const char* mode = "clamp";
#elif defined(ATTO_NUMERIC_MIN)
extern "C" std::int64_t oresforge_entry(std::int64_t left, std::int64_t right);
constexpr const char* mode = "min";
#else
extern "C" std::int64_t oresforge_entry(std::int64_t left, std::int64_t right);
constexpr const char* mode = "max";
#endif

static_assert(sizeof(std::int64_t) == 8);
constexpr std::size_t count = 10000;
constexpr std::array<std::int64_t, 9> edges = {
    std::numeric_limits<std::int64_t>::min(),
    std::numeric_limits<std::int64_t>::min() + 1,
    -43, -1, 0, 1, 42,
    std::numeric_limits<std::int64_t>::max() - 1,
    std::numeric_limits<std::int64_t>::max()
};
struct Generator {
  std::uint64_t state = 0x6a09e667f3bcc909ULL;
  std::uint64_t next() {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }
  std::int64_t signed_next() { return std::bit_cast<std::int64_t>(next()); }
};
int main() {
  Generator rng;
  std::uint64_t digest = 14695981039346656037ULL;
  for (std::size_t i = 0; i < count; ++i) {
    std::int64_t a, b, c;
    if (i < edges.size() * edges.size() * edges.size()) {
      a = edges[(i / (edges.size() * edges.size())) % edges.size()];
      b = edges[(i / edges.size()) % edges.size()];
      c = edges[i % edges.size()];
    } else {
      a = rng.signed_next();
      b = rng.signed_next();
      c = rng.signed_next();
    }
    std::int64_t expected, actual;
#if defined(ATTO_NUMERIC_MIN)
    expected = a < b ? a : b;
    actual = oresforge_entry(a, b);
#elif defined(ATTO_NUMERIC_MAX)
    expected = a > b ? a : b;
    actual = oresforge_entry(a, b);
#else
    expected = b > c ? b : (a < b ? b : (a > c ? c : a));
    actual = oresforge_entry(a, b, c);
#endif
    if (actual != expected) {
      std::fprintf(stderr,
        "ATTO_NUMERIC_DIFFERENTIAL_MISMATCH|mode=%s|case=%zu|a=%lld|b=%lld|c=%lld|actual=%lld|expected=%lld\n",
        mode, i, static_cast<long long>(a), static_cast<long long>(b),
        static_cast<long long>(c), static_cast<long long>(actual),
        static_cast<long long>(expected));
      return 1;
    }
    digest ^= std::bit_cast<std::uint64_t>(actual);
    digest *= 1099511628211ULL;
  }
  std::printf("ATTO_FORGE_NUMERIC_DIFFERENTIAL_EXECUTED|mode=%s|cases=%zu|checksum=%016llx|native=YES|hosted_harness=YES\n",
              mode, count, static_cast<unsigned long long>(digest));
  return 0;
}
