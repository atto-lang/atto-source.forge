#include "atto/result_int_parse_v1.hpp"
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

using atto::forge::i64;
using atto::forge::result_v1::TenantParser;
static void require(bool yes) { if (!yes) std::abort(); }
int main() {
  TenantParser a(101), b(102);
  for (i64 n = -20000; n <= 20000; ++n) {
    auto result = a.parse(std::to_string(n));
    require(result.is_ok() && result.value == n);
  }
  for (const auto& p : {
    std::pair{"0", i64{0}},
    {"+42", i64{42}},
    {"-0", i64{0}},
    {"00042", i64{42}},
    {"-9223372036854775808", std::numeric_limits<i64>::min()},
    {"9223372036854775807", std::numeric_limits<i64>::max()}
  }) {
    auto result = a.parse(p.first);
    require(result.is_ok() && result.value == p.second);
  }
  for (std::string_view raw : {
    "", "+", "-", " 42", "42 ", "++1", "-+1",
    "42.0", "42e0", "4_2", "42x", "9223372036854775808",
    "-9223372036854775809", "٠١", "٤٢", "\t42"
  }) {
    auto failed = a.parse(raw);
    require(failed.is_err() && failed.error.message == "invalid integer");
    require(a.can_observe(failed.error));
    require(!b.can_observe(failed.error));
  }
  const std::string nul("4\0" "2", 3);
  require(a.parse(nul).is_err());
  require(a.parse(std::string(1025, '0')).is_err());
  require(a.parse(std::string(1024, '0')).is_ok());
  auto first = a.parse("bad");
  auto propagated = first; // same identity, no new Error
  auto second = a.parse("also bad");
  require(first.is_err() && second.is_err());
  require(propagated.error.identity == first.error.identity);
  require(second.error.identity != first.error.identity);
  require(first.error.tenant_id == 101);
  require(b.parse("bad").error.identity == 1);
  bool rejected = false;
  try { (void)TenantParser(0); }
  catch (const std::invalid_argument&) { rejected = true; }
  require(rejected);
}
