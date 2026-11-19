#include "atto/scalar_parser.hpp"
#include <cassert>
#include <limits>
#include <string>
using namespace atto::forge;
using namespace atto::forge::scalar;
int main() {
  auto a = parse_source("pub fnc main(): int { return 4 + 5 * (6 - 1); }");
  assert(a.ok() && a.nodes[a.root].op == Op::add);
  assert(a.nodes.size() == 7);
  auto ops = parse_source("fnc main(): int {return ~0 & 15 | 1 ^ 2;}");
  assert(ops.ok() && ops.nodes[ops.root].op == Op::bor);
  auto b = parse_source("fnc main(): int {return -9223372036854775808;}");
  assert(b.ok() && b.nodes[b.root].value == std::numeric_limits<i64>::min());
  auto padded_min = parse_source("fnc main(): int {return -0009223372036854775808;}");
  assert(padded_min.ok() &&
         padded_min.nodes[padded_min.root].value == std::numeric_limits<i64>::min());
  auto padded_over = parse_source("fnc main(): int {return -0009223372036854775809;}");
  assert(!padded_over.ok() &&
         padded_over.error == ParseError::literal_out_of_range);
  auto c = parse_source("fnc main(): int {return -(-9223372036854775808);}");
  assert(c.ok() && c.nodes[c.root].op == Op::neg);
  for (const auto* bad : {
    "", "fnc nope(): int {return 1;}", "fnc main(int x): int {return 1;}",
    "fnc main(): int {return 1; return 2;}",
    "fnc main(): int {return 1}","fnc main(): int {return 1; } junk",
    "fnc main(): int {return 1 + ;}",
    "fnc main(): int {return 9223372036854775808;}",
    "fnc main(): int {return -9223372036854775809;}",
    "fnc main(): int {return x;}",
    "fnc main(): int {return 42 == 42;}",
    "fnc main(): int {return 2 ** 3;}",
    "fnc main(): int {return (1;}",
    "fnc main(): int {return 1;}\nfnc evil():int{return 2;}",
    "fnc main(): int {return @;}"
  }) assert(!parse_source(bad).ok());
  auto overflow = parse_source("fnc main(): int {return 9223372036854775808;}");
  assert(overflow.error == ParseError::literal_out_of_range);
  std::string deep(40, '(');
  deep += "1";
  deep += std::string(40, ')');
  auto nested = parse_source("fnc main(): int { return " + deep + "; }");
  assert(nested.error == ParseError::depth_limit);
  std::string many = "fnc main(): int {return 1";
  for (int i = 0; i < 70; ++i) many += "+1";
  many += ";}";
  auto budget = parse_source(many);
  assert(budget.error == ParseError::node_limit);
  assert(parse_source(std::string(16385, 'a')).error == ParseError::lexical);
}
