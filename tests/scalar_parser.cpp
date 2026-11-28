#include "atto/scalar_parser.hpp"
#include <cassert>
#include <limits>
#include <string>
using namespace atto::forge;
using namespace atto::forge::scalar;
int main() {
  // Contiguous double-angle I/O is a distinct unsupported expression,
  // not a numeric shift. Preserve exact byte offsets and strict rejection.
  for (const auto* source : {
    "fnc main(): int {return 1 >>;}",
    "fnc main(): int {return (1 >>);}",
    "fnc main(): int {return 1 << 2;}",
    "fnc main(): int {return 1<<2;}",
    "fnc main(): int {return 1 >> 2;}",
    "fnc main(): int {return << 1;}"
  }) {
    const auto rejected = parse_source(source);
    assert(!rejected.ok());
    assert(rejected.error == ParseError::io_direction_unsupported);
    assert(parse_error_name(rejected.error) == "E_IO_DIRECTION_UNSUPPORTED");
    const std::string_view input(source);
    const auto offset = input.find("<<") != std::string_view::npos
      ? input.find("<<") : input.find(">>");
    assert(rejected.error_offset == offset);
  }
  for (const auto* source : {"fnc main():int{return 1 < < 2;}",
                             "fnc main():int{return 1 > > 2;}"}) {
    const auto rejected = parse_source(source);
    assert(!rejected.ok());
    assert(rejected.error != ParseError::io_direction_unsupported);
  }
  assert(parse_source("fnc main():int{return 1 <<< 3 | 8 >>> 2;}").ok());
  assert(parse_source("fnc main():int{/* outer /* << >> */ inner */ return 42;}").ok());
  assert(parse_source("fnc main():int{return <int>42;}").ok());
  // Bounded catalog allowance must not change ordinary or strict lexing.
  std::string catalog = "define dimension Length; define unit Meter: Length;";
  for(int i=0;i<48;++i) catalog += "define unit Measure"+std::to_string(i)+": Length = Meter * 1;";
  catalog += "type Positive = int where self > 0; type Negative = int where self < 0;";
  const auto small = std::string("pub fnc main() -> int {val a=42<Measure47>; return a/1<Measure47>;}");
  assert(parse_source(catalog+small).ok());
  assert(!parse_verified_strict_i64(catalog+small).ok());
  std::string large = "pub fnc main() -> int {return 1";
  for(int i=0;i<260;++i)large += "+1";
  large += ";}";
  assert(!parse_source(catalog+large).ok());
  assert(!parse_source(large).ok());
  assert(!tokenize(std::string(max_tokens+1,'+')).ok());
  assert(!tokenize(std::string(max_quantity_tokens+1,'+'),max_quantity_tokens).ok());
  // The old unit-only cap rejected a valid 33rd declaration as generic syntax.
  // The lexical and dimension budgets remain unchanged.
  std::string unit_catalog = "define dimension Length; define unit Meter: Length;";
  for (int i=0;i<32;++i) {
    unit_catalog += "define unit Unit" + std::to_string(i) + ": Length = Meter * 1;";
  }
  assert(parse_source(unit_catalog + "type Positive = int where self > 0; pub fnc main() -> int {val a=42<Unit31>; return a/1<Unit31>;}").ok());
  const auto duplicate = parse_source(unit_catalog + "define unit Unit31: Length = Meter * 1; pub fnc main() -> int {return 42;}");
  assert(duplicate.error == ParseError::unit_duplicate);
  assert(duplicate.nodes.empty());
  for(const auto* source : {
    "fnc main():int {const x:=42;const Pair<int> p=x;return p.value;}",
    "fnc main():int {const x=x:=42;const Pair<int> p=x;return p.value;}",
    "fnc main():int {const x=Pair.of(\"x\",42);const Pair<int> p=x;return p.value;}",
    "fnc main():int {const x=\"x\":=42;return x.value;}",
    "fnc main():int {const x=y:=42;return x.value;}",
    "fnc main():int {const x:=40;const r={y:2};return x.value+r.y;}"
  }) assert(parse_source(source).ok());
  for(const auto* source : {
    "fnc main():int {const x:=1;const x:=2;return 0;}",
    "fnc main():int {const x:=x;return 0;}",
    "fnc main():int {const x=x:=x;return 0;}",
    "fnc main():int {const x:=true;return 0;}",
    "fnc main():int {const x:=42;return x;}",
    "fnc main():int {const x:=42;return x.key;}",
    "fnc main():int {const x:=42;x.value=7;return 0;}",
    "fnc main():int {const r={value:42};const Pair<int> p=r;return 0;}",
    "fnc main():int {const x:=42;const r={...x};return 0;}",
    "fnc main():int {const x=Pair.of(7,42);return 0;}",
    "fnc main():int {const x:=42;const x={a:1};return 0;}",
    "fnc main():int {const x:=9223372036854775808;return 0;}"
  }) assert(!parse_source(source).ok());
  for(const auto* source : {
    "fnc main():int {const r={foo:=40,bar:2};return r.foo+r.bar;}",
    "fnc main():int {const Record r=(foo:=40,bar=2);return r.foo+r.bar;}",
    "fnc main():int {const r=(foo:40,bar:=2);return r.foo+r.bar;}",
    "fnc main():int {const r={foo=40,bar:=2,};return r.foo+r.bar;}",
    "fnc main():int {const r={foo:44};const s={bar:-2};return r.foo+s.bar;}"
  }) assert(parse_source(source).ok());
  for(const auto* source : {
    "fnc main():int {const r={foo:=1,foo:2};return 0;}",
    "fnc main():int {const r=(foo:=1,bar:=2);return 0;}",
    "fnc main():int {const r={foo : = 1};return 0;}",
    "fnc main():int {const r={foo:=1 bar:=2};return 0;}",
    "fnc main():int {const r={foo:1};return r;}",
    "fnc main():int {const r={foo:1};return r.bar;}",
    "fnc main():int {const r={foo:1};const r={foo:2};return 0;}",
    "fnc main():int {const r={foo:1};r.foo=2;return 0;}",
    "fnc main():int {const r={foo:1+2};return 0;}",
    "fnc main():int {const r={foo:true};return 0;}",
    "fnc main():int {const r={foo:9223372036854775808};return 0;}"
  }) assert(!parse_source(source).ok());
  for (const auto* call : {"stdio.out.write(\"A\")", "process.stdout.write(\"A\")",
                           "stdio /* inert */ .out.write(\"A\")", "process.stdout /* nested /* ok */ */ .write(\"A\")"}) {
    const auto source = std::string("pub fnc main(): int {") + call + "; return 42;}";
    for (const auto& rejected : {parse_source(source), parse_verified_strict_i64(source)}) {
      assert(rejected.error == ParseError::stdout_profile_unsupported);
      assert(parse_error_name(rejected.error) == "E_STDOUT_PROFILE_UNSUPPORTED");
      assert(rejected.nodes.empty());
    }
  }
  assert(parse_source("fnc main(): int {return 42; /* stdio.out.write(\"A\") */}").ok());
  assert(parse_source("fnc main(): int {return \"process.stdout.write(42)\";}").error != ParseError::stdout_profile_unsupported);
  assert(parse_source("fnc main(): int {return object.stdio.out.write(42);}").error != ParseError::stdout_profile_unsupported);

  assert(parse_source("fnc main(): int {val Φ = 20; val φ = 22; return Φ + φ;}").ok());
  assert(parse_source("fnc main(): int {val 数量 = 20; val 𠮷 = 22; return 数量 + 𠮷;}").ok());
  assert(!parse_source("fnc main(): int {return 未定義;}").ok());
  // Grouped actor options cannot reach scalar IR or guest emission.
  for (const auto* expr : {
      "spawn Worker() with {}",
      "spawn Worker() with { schedule { max_parallelism = 2; affinity = sticky; } }",
      "spawn Worker() with { resources { heap_size = 64 * 1024 * 1024; stack_size = 65536; mailbox_capacity = 1024; } }",
      "spawn Worker() with { init { name = \"worker-1\"; seed = 42; } }",
      "await spawn Worker() with {}",
      "spawn /* nested /* comment */ ok */ Worker() with {}",
      "spawn Worker() with schedule(max_parallelism=2)",
      "spawn Worker()"
  }) {
    const auto source = std::string("fnc main(): int {return ") + expr + ";}";
    const auto rejected = parse_source(source);
    assert(rejected.error == ParseError::spawn_options_unsupported);
    assert(parse_error_name(rejected.error) == "E_SPAWN_OPTIONS_UNSUPPORTED");
    assert(rejected.nodes.empty());
  }
  assert(parse_source("fnc main(): int {return 42; /* spawn Worker() with {} */}").ok());
  assert(parse_source("fnc main(): int {return \"spawn Worker() with {}\";}").error != ParseError::spawn_options_unsupported);
  assert(parse_source("fnc main(): int {return process.spawn(42);}").error != ParseError::spawn_options_unsupported);
  // All four effect spellings must fail closed in the scalar profile.
  for (const std::string_view spelling : {"suspends", "susp", "\xE2\xA7\x96", "\xE2\x88\x82"}) {
    const auto source = std::string("pub ") + std::string(spelling) +
        " fnc main(): int {return 42;}";
    const auto lexed = tokenize(source);
    assert(lexed.error == LexError::suspension_unsupported);
    assert(lexed.error_offset == 4);
    const auto rejected = parse_source(source);
    assert(rejected.error == ParseError::suspension_unsupported);
    assert(parse_error_name(rejected.error) == "E_SUSPENDS_UNSUPPORTED");
    const auto comment = std::string("fnc main(): int {return 42;} // ") + std::string(spelling);
    assert(parse_source(comment).ok());
    const auto quoted = tokenize(std::string("\"") + std::string(spelling) + "\"");
    assert(quoted.ok() && quoted.tokens[0].kind == Kind::quoted);
  }
  assert(tokenize("suspension susp2").ok());

  // Pointer<int> ref!/deref!/unref! is admitted only by the distinct
  // closed-world local-pointer parser, never as an untyped ownership call.
  for (const std::string_view expression : {
      "copy!42", "take!value", "borrow! mut owner",
      "share!owner", "pin!owner", "drop!owner",
      "freeze!owner", "weak!owner", "copy!eq\xC3\xA9" }) {
    const auto source = std::string("fnc main(): int {return ") +
                        std::string(expression) + ";}";
    const auto rejected = parse_source(source);
    assert(rejected.error == ParseError::ownership_bang_unsupported);
    assert(parse_error_name(rejected.error) == "E_OWNERSHIP_BANG_UNSUPPORTED");
  }
  const auto ordinary_comparison =
      parse_source("fnc main(): int { return copy!eq other; }");
  assert(ordinary_comparison.error != ParseError::ownership_bang_unsupported);
  const auto nested_comment = parse_source(
      "fnc main(): int {return /* outer /* copy!secret */ remaining */ 42;}");
  assert(nested_comment.ok());
  assert(nested_comment.nodes[nested_comment.root].value == 42);
  // Even at the maximum supported depth, ownership-looking text remains
  // comment trivia and cannot reach typed pointer or scalar lowering.
  std::string deep_comment = "fnc main(): int {return ";
  for (int n = 0; n < 128; ++n) deep_comment += "/*";
  deep_comment += " copy!untrusted_secret ";
  for (int n = 0; n < 128; ++n) deep_comment += "*/";
  deep_comment += " 42;}";
  const auto deep_parsed = parse_source(deep_comment);
  assert(deep_parsed.ok());
  assert(deep_parsed.nodes[deep_parsed.root].value == 42);
  std::string overdeep_comment = "fnc main(): int {return ";
  for (int n = 0; n < 129; ++n) overdeep_comment += "/*";
  overdeep_comment += " copy!untrusted_secret ";
  for (int n = 0; n < 129; ++n) overdeep_comment += "*/";
  overdeep_comment += " 42;}";
  assert(parse_source(overdeep_comment).error == ParseError::lexical);
  auto commented = parse_source(
      "/* module header */ fnc main(): int { // body\n"
      " return 8 / /* denominator */ 2; /* tail */ }");
  assert(commented.ok());
  // Constructor admission, not a lock implementation.
  auto bare_mutex = parse_source("pub fnc main(): int {return Mutex<int>(0); }");
  assert(!bare_mutex.ok() && bare_mutex.error == ParseError::new_required);
  assert(parse_error_name(bare_mutex.error) == "E_NEW_REQUIRED");
  auto new_mutex = parse_source("pub fnc main(): int {return new Mutex<int>(0); }");
  assert(!new_mutex.ok() && new_mutex.error == ParseError::lock_unsupported);
  assert(parse_error_name(new_mutex.error) == "E_LOCK_UNSUPPORTED");
  auto nested_mutex = parse_source("pub fnc main(): int {return Mutex<Option<int>>(0); }");
  assert(!nested_mutex.ok() && nested_mutex.error == ParseError::new_required);
  assert(parse_source("// Mutex<int>(0) is commented out\nfnc main():int{return 42;}").ok());
  assert(!parse_source("fnc main():int{return Mutex<int(0);}").ok());
  assert(!parse_source("fnc main():int{return Mutex<>(0);}").ok());
  assert(parse_source("fnc main():int{return other.Mutex<int>(0);}").error != ParseError::new_required);


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
  assert(parse_source("fnc main(): int { ^ 12 ^^ 10; }").ok());
  for (const auto* bad_caret : {"fnc main(): int {return 12 ^ 10;}",
       "fnc main(): int {return 12 ^ ^ 10;}", "fnc main(): int {^^ 42;}",
       "fnc main(): int {return true ^^ false;}"})
    assert(!parse_source(bad_caret).ok());
  auto ops = parse_source("fnc main(): int {return ~0 & 15 | 1 ^^ 2;}");
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
  assert(parse_source("fnc main(): int {return 4_2;}").ok());
  assert(parse_source("fnc main(): int {return -9_223_372_036_854_775_808;}").ok());
  for(auto bad : {"1_","1__2","9_223_372_036_854_775_808"}) {
    assert(!parse_source(std::string("fnc main(): int {return ")+bad+";}").ok());
  }
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
  for (std::size_t i = 0; i < max_nodes/2+6; ++i) many += "+1";
  many += ";}";
  auto budget = parse_source(many);
  assert(budget.error == ParseError::node_limit);
  assert(!tokenize(std::string(max_strict_tokens+1,'+'),max_strict_tokens).ok());
  assert(parse_source(std::string(16385, 'a')).error == ParseError::lexical);
}
