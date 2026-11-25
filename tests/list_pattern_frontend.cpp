// Native frontend properties for the real .atto -> C++20 List guest slice.
// Executable guest proof is separate (list_pattern_guest.cmake).
#include "atto/list_pattern_guest.hpp"
#include <cassert>
#include <string>
#include <string_view>

namespace p=atto::forge::list_guest;
static constexpr std::string_view base=R"atto(
pub fnc main(): int {
  val items: List<int> = List.of(10, 20, 30, 40, 50);
  return match<int> items over
    on List.of(..., is int as val a, is int as val b, is int as val c, ...) -> a + b + c,
    on _ -> 0
  end;
}
)atto";
static void fail(std::string_view source,p::Error expected) {
  auto got=p::compile(source);
  assert(got.candidate && got.error==expected && got.source.empty());
}
int main() {
  const auto good=p::compile(base);
  assert(good.ok());
  assert(good.source.find("std::array<atto::forge::i64,5>")!=std::string::npos);
  assert(good.source.find("atto::forge::checked_add")!=std::string::npos);
  assert(good.source.find("values[start+")!=std::string::npos);
  assert(good.source.find("a + b + c")==std::string::npos);
  assert(good.source.find("10,20,30,40,50")!=std::string::npos);

  // Casing, canonical syntax, and source provenance are not user callbacks.
  assert(!p::is_candidate("pub fnc main(): int { /* List<int> */ return 42; }"));
  assert(!p::is_candidate("pub fnc main(): int { // List.of(1)\nreturn 42; }"));
  assert(!p::compile("pub fnc main(): int { return 42; }").candidate);
  assert(!p::compile("fnc main(): int {return 42;}").candidate);
  const auto good2=p::compile(base);
  assert(good.source==good2.source);

  auto substitute=[](std::string text,std::string_view before,std::string_view after) {
    const auto pos=text.find(before);
    assert(pos!=std::string::npos);
    text.replace(pos,before.size(),after);
    return text;
  };
  fail(substitute(std::string(base),"is int as val b","is int as val a"),
       p::Error::binding);
  fail(substitute(std::string(base),"a + b + c","a + missing + c"),
       p::Error::binding);
  fail(substitute(std::string(base),"is int as val a","is int as val return"),
       p::Error::reserved);
  fail(substitute(std::string(base),"is int as val a","is int as val yield"),
       p::Error::reserved);
  fail(substitute(std::string(base),"val items: List<int>","val namespace: List<int>"),
       p::Error::reserved);
  // 'main' is a callable entrypoint spelling, not a reserved local name
  // in the canonical Rust parser::RESERVED list.
  const auto main_local=substitute(
      substitute(std::string(base),"val items: List<int>","val main: List<int>"),
      "match<int> items over","match<int> main over");
  assert(p::compile(main_local).ok());

  fail(substitute(std::string(base),"on _ -> 0","on _ -> a"),
       p::Error::binding);
  fail(substitute(std::string(base),"..., is int as val a","..., ..., is int as val a"),
       p::Error::source);
  fail(substitute(std::string(base),"is int as val b, is int as val c",
       "is int as val b, ..., is int as val c"),p::Error::unsupported);
  fail(substitute(std::string(base),"List.of(10, 20, 30, 40, 50)",
       "List.of(10, 20,)"),p::Error::source);
  fail(substitute(std::string(base),"..., is int as val a",
       "pattern.of<int>(\"...\"), is int as val a"),p::Error::unsupported);
  fail(substitute(std::string(base),"..., is int as val a",
       "...(3 of int), is int as val a"),p::Error::unsupported);
  fail(substitute(std::string(base),"is int as val b","is bool as val b"),
       p::Error::unsupported);
  fail(substitute(std::string(base),"val items: List<int>","let items: List<int>"),
       p::Error::unsupported);

  std::string oversized="pub fnc main(): int { val items: List<int> = List.of(";
  for (int i=0;i<33;++i) {
    if(i!=0)oversized+=",";
    oversized+=std::to_string(i);
  }
  oversized+="); return match<int> items over on List.of(...) -> 1, on _ -> 0 end; }";
  fail(oversized,p::Error::budget);

  const std::string empty=
      "pub fnc main(): int { val items: List<int> = List.of(); "
      "return match<int> items over on List.of(...) -> 1, on _ -> 0 end; }";
  assert(p::compile(empty).ok());
  const std::string min=
      "pub fnc main(): int { val items: List<int> = List.of(-9223372036854775808); "
      "return match<int> items over on List.of(is int as val a) -> a, on _ -> 0 end; }";
  assert(p::compile(min).ok());
  const std::string too_high=
      "pub fnc main(): int { val items: List<int> = List.of(9223372036854775808); "
      "return match<int> items over on List.of(...) -> 1, on _ -> 0 end; }";
  fail(too_high,p::Error::range);
  const std::string comment=
      "/* List<int> List.of() */ pub fnc main(): int { return 42; }";
  assert(!p::is_candidate(comment));
  const std::string guard=
      "pub fnc main(): int { val items: List<int> = List.of(-1,5,10,20); "
      "return match<int> items over on List.of(...,is int as val a,is int as val b,...) "
      "when a > 0 -> a + b, on _ -> 0 end; }";
  assert(p::compile(guard).ok());

  // Bounded denial: no user string or bytecode is ever embedded in generated C++.
  fail(std::string(base)+std::string(17*1024,'x'),p::Error::budget);
}
