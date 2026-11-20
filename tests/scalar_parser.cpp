#include "atto/scalar_parser.hpp"
#include <cassert>
#include <limits>
#include <string>
using namespace atto::forge;
using namespace atto::forge::scalar;
int main() {
  auto commented = parse_source(
      "/* module header */ fnc main(): int { // body\n"
      " return 8 / /* denominator */ 2; /* tail */ }");
  assert(commented.ok());
  auto malformed_comment = parse_source("fnc main(): int {return 1; /*");
  assert(!malformed_comment.ok() && malformed_comment.error == ParseError::lexical);
  for (const auto* signature : {
    "fnc main() -> int {return 42;}",
    "fnc main() : int -> {return 42;}",
    "fnc int <- main() {return 42;}",
    "int <- main() {return 42;}",
    "pub fnc int <- main() {return 42;}",
    "pub fnc main() -> int {return 42;}"
  }) assert(parse_source(signature).ok());
  for (const auto* inferred : {
    "fnc main(): infer {return 40+2;}",
    "fnc main() -> infer {return 6*7;}",
    "fnc main(): infer -> {return 42;}",
    "fnc infer <- main() {return 42;}",
    "infer <- main() {return 42;}",
    "pub fnc main() -> infer {return -1 >>> 1;}"
  }) assert(parse_source(inferred).ok());
  for (const auto* bad_inferred : {
    "fnc main(): infer {return;}",
    "fnc main() -> infer {return true;}",
    "fnc main(): infer {return \"hello\";}",
    "fnc main() -> infer -> {return 42;}",
    "fnc main() -> bool {return 42;}"
  }) assert(!parse_source(bad_inferred).ok());
  for (const auto* bad_signature : {
    "fnc main() -> int -> {return 42;}",
    "fnc main() -> {return 42;}",
    "fnc main() : -> int {return 42;}",
    "fnc int <- other() {return 42;}",
    "fnc main() <- int {return 42;}",
    "fnc main() -> string {return 42;}",
    "int -> main() {return 42;}"
  }) assert(!parse_source(bad_signature).ok());
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
  const std::string within(max_parse_depth, '(');
  const std::string close(max_parse_depth, ')');
  assert(parse_source("fnc main(): int {return " + within + "42" + close + ";}").ok());
  assert(parse_source("fnc main(): int {return " + within + "(42)" + close + ";}").error == ParseError::depth_limit);
  const std::string unary_within(max_parse_depth, '~');
  assert(parse_source("fnc main(): int {return " + unary_within + "42;}").ok());
  assert(parse_source("fnc main(): int {return " + unary_within + "~42;}").error == ParseError::depth_limit);
  std::string flat = "fnc main(): int {return 1";
  for (int i = 0; i < 60; ++i) flat += "+1";
  flat += ";}";
  assert(parse_source(flat).ok());
  assert(nested.error == ParseError::depth_limit);
  std::string many = "fnc main(): int {return 1";
  for (int i = 0; i < 70; ++i) many += "+1";
  many += ";}";
  auto budget = parse_source(many);
  assert(budget.error == ParseError::node_limit);
  assert(parse_source(std::string(16385, 'a')).error == ParseError::lexical);
  // Compiler backend parity target: BOTH whole-Entry ascriptions are
  // recognized as unlowered typed constructs, never a scalar i64 value.
  // These are negative admission checks, not positive guest execution.
  for (const auto* src : {
    R"(let entry1 = ("foo" => 5) as Entry<string,int>;
        let entry2 = "foo" => 5 (as Entry<string,int>);
        pub fnc main():int {
          let mut Map<string,int> x = {"foo" => 5};
          x.set("foo",6); x["foo"] = 3;
          x.add(entry1); x.add(entry2);
          return x.get("foo").unwrap();
        })",
    R"(pub fnc main():int { let e = "foo" => 5 (as Entry<string,int>); return 0; })",
    R"(pub fnc main():int { return 1 => 2; })",
  }) {
    auto blocked = parse_source(src);
    assert(!blocked.ok() && blocked.error == ParseError::unsupported_map_entry);
  }
  auto record = parse_source(
      R"(pub fnc main():int { let mut Record r = {foo:1}; r["foo"] = 3; return 0; })");
  assert(record.error == ParseError::unsupported_record);
  auto nominal = parse_source(
      R"(define struct Point as int foo; end pub fnc main():int { p["foo"] = 3; return 0; })");
  assert(nominal.error == ParseError::unsupported_index);
  auto commented_collection = parse_source(
      "// Entry<string,int> => Map[foo] is comment only\\n"
      "/* Record r[foo] = 3; */ pub fnc main():int { return 42; }");
  assert(commented_collection.ok());
  assert(commented_collection.nodes[commented_collection.root].value == 42);
  auto quoted_name = parse_source(R"(fnc main():int { return "Entry Map => Record[foo]"; })");
  assert(quoted_name.error == ParseError::syntax);
  assert(parse_error_name(ParseError::unsupported_map_entry) == "E_UNSUPPORTED_MAP_ENTRY");
  assert(parse_error_name(ParseError::unsupported_record) == "E_UNSUPPORTED_RECORD");
  assert(parse_error_name(ParseError::unsupported_index) == "E_UNSUPPORTED_INDEX_WRITE");

}
