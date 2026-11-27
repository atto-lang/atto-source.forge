#include "atto/physical_layout.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
using namespace atto::forge::layout;
struct Payload {std::int64_t count;std::int8_t flag;};
struct Packet {std::int8_t tag;Payload items[2];std::int16_t tail;};
int main(){
  const auto s=[](std::uint64_t n){return Type{Kind::scalar,{n,n},0,1};};
  const std::vector<Decl> declarations={
    {"Packet",{{"tag",s(1)},{"items",{Kind::nominal_array,{0,0},1,2}},{"tail",s(2)}}},
    {"Payload",{{"count",s(8)},{"flag",s(1)}}}
  };
  const auto p=plan(declarations);assert(p);
  assert(((*p)[0].storage==Storage{sizeof(Packet),alignof(Packet)}));
  assert(((*p)[1].storage==Storage{sizeof(Payload),alignof(Payload)}));
  assert(project(*p,0,{1,1,0})->first==offsetof(Packet,items)+sizeof(Payload)+offsetof(Payload,count));
  assert(project(*p,0,{1,1,1})->first==offsetof(Packet,items)+sizeof(Payload)+offsetof(Payload,flag));
  assert(project(*p,0,{2})->first==offsetof(Packet,tail));
  assert((project(*p,0,{1})->second==Storage{sizeof(Payload)*2,alignof(Payload)}));
  assert(!project(*p,0,{1,2,0}));assert(!project(*p,0,{1,1,2}));assert(!project(*p,0,{0,0}));
  auto bad=declarations;bad[0].fields[1].second.count=0;assert(!plan(bad));
  bad=declarations;bad[0].fields[1].second.owner=0;assert(!plan(bad));
  bad=declarations;bad[0].fields[1].second.owner=2;assert(!plan(bad));
  bad=declarations;bad[0].fields[1].second.count=UINT64_MAX;assert(!plan(bad));
  bad=declarations;bad[0].fields[1].second={Kind::scalar_array,{2,2},0,3};auto scalar=plan(bad);assert(scalar);
  assert(((*scalar)[0].storage==Storage{10,2}));assert(project(*scalar,0,{1,2})->first==6);
  assert(!project(*scalar,0,{1,3}));assert(!project(*scalar,0,{1,0,0}));
  bad=declarations;bad[0].fields[0].second={Kind::scalar,{3,2},0,1};assert(!plan(bad));
  bad=declarations;bad[0].fields[0].second={Kind::scalar,{1,1},0,2};assert(!plan(bad));
  bad=declarations;bad[0].fields[0].first="items";assert(!plan(bad));
  bad=declarations;bad[0].identity="Payload";assert(!plan(bad));
  assert(!plan(declarations,Limits{47,8,256,4096}));
  std::vector<Decl> deep;
  for(std::size_t i=0;i<256;++i)deep.push_back({std::to_string(i),{{"cell",i==255?s(8):Type{Kind::nominal,{0,0},i+1,1}}}});
  auto chain=plan(deep);assert(chain&&(*chain)[0].storage.size==8);
  deep.back().fields[0].second={Kind::nominal,{0,0},0,1};assert(!plan(deep));
}
