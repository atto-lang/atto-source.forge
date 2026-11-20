// HOST-ONLY native pattern reference tests. No Atto guest source compiled.
#include "atto/typed_pattern_fragment.hpp"
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

using atto::forge::pattern::Type;
using atto::forge::pattern::Kind;
using atto::forge::pattern::Error;
using atto::forge::pattern::compile;
using atto::forge::pattern::reference_match_i64;
namespace p = atto::forge::pattern;

static p::Compiled parse(std::string_view source, Type type=Type::integer) {
  return compile(source,type,std::nullopt,true,true,true);
}
static void rejection(std::string_view source, Error expected,
                      Type ty=Type::integer) {
  const auto c=parse(source,ty);
  assert(c.error==expected);
  assert(!c.ok());
}
int main() {
  const auto inferred=parse("...");
  const auto explicit_typed=compile("...",Type::integer,Type::integer,true,true,true);
  assert(inferred.ok() && explicit_typed.ok());
  assert(inferred.fragment.items.size()==1 && explicit_typed.fragment.items.size()==1);
  assert(inferred.fragment.items[0].kind==Kind::rest);
  assert(compile("...",Type::any,Type::integer,true,true,true).error==Error::type);
  assert(compile("...",Type::integer,{},false,true,true).error==Error::context);
  assert(compile("...",Type::integer,{},true,false,true).error==Error::context);
  assert(compile("...",Type::integer,{},true,true,false).error==Error::constexpr_required);

  const auto counted=parse("...(3 of int)");
  assert(counted.ok() && counted.fragment.items.size()==3);
  for(const auto& a:counted.fragment.items)assert(a.kind==Kind::is_type && a.type==Type::integer);
  const auto repeated=parse("...(3 of 7)");
  assert(repeated.ok());
  assert(reference_match_i64(repeated.fragment,{7,7,7}).matched);
  assert(!reference_match_i64(repeated.fragment,{7,8,7}).matched);
  const auto zero=parse("...(0 of int), 9");
  assert(zero.ok() && zero.fragment.items.size()==1);

  const auto left=parse("...");
  const auto right=parse("7, ...");
  const auto combined=p::compose({left.fragment,right.fragment},Type::integer);
  assert(combined.ok() && combined.fragment.items.size()==3);
  assert(p::compose({left.fragment,left.fragment},Type::integer).error==Error::source);
  assert(p::compose({left.fragment,parse("...",Type::any).fragment},Type::integer).error==Error::type);

  const auto in_list=parse("..., 3, 4, 5, ...");
  assert(in_list.ok());
  auto hit=reference_match_i64(in_list.fragment,{1,2,3,4,5,9});
  assert(hit.error==Error::none && hit.matched);
  assert(hit.rest_regions.size()==2);
  assert((hit.rest_regions[0]==std::pair<std::size_t,std::size_t>(0,2)));
  assert((hit.rest_regions[1]==std::pair<std::size_t,std::size_t>(5,6)));
  assert(!reference_match_i64(in_list.fragment,{1,3,4,8,5}).matched);
  const auto twice=parse("..., 3, ..., 3");
  assert(twice.ok());
  assert(!reference_match_i64(twice.fragment,{3}).matched);
  hit=reference_match_i64(twice.fragment,{3,3});
  assert(hit.matched && hit.rest_regions.size()==2);
  assert((hit.rest_regions[0]==std::pair<std::size_t,std::size_t>(0,0)));
  assert((hit.rest_regions[1]==std::pair<std::size_t,std::size_t>(1,1)));

  assert(parse("is int",Type::any).ok());
  assert(parse("is !bool",Type::integer).ok());
  rejection("is !int",Error::type);
  rejection("is bool",Error::type);
  rejection("8",Error::type,Type::text);
  rejection("true",Error::type);
  assert(parse("\"a,b\"",Type::text).ok());
  assert(parse("\"a\\\"b\"",Type::text).ok());

  for(const auto invalid:{
    "is int as val x", "get_secret()", "await read()", "..._", "_...",
    "1 as val x", "...(2 of ...)", "...(2 of ...(1 of int))",
    "...(2..4 of int)", "...(n of int)", "...(3 of List.of(1))",
    "...(1 of is int as val x)", "...(3 of int", "...,",
    "..., ...", "1, (2,3)", "\"bad\\z\""
  }) rejection(invalid,Error::source,Type::any);
  rejection("...(129 of int)",Error::budget);
  rejection("...(9999999999999999999999 of int)",Error::budget);
  rejection(std::string(4097,'x'),Error::budget);
  rejection(std::string("\xE2\x80\xAE")+"...",Error::source,Type::any);

  const auto oversized=reference_match_i64(parse("...").fragment,
      std::vector<std::int64_t>(257,1));
  assert(oversized.error==Error::budget && !oversized.matched);
  assert(reference_match_i64(parse("...",Type::text).fragment,{1}).error==Error::type);
}
