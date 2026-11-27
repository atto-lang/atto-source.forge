#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace atto::forge::layout {
// Explicit target ABI facts, not a loan certificate or source admission.
struct Storage { std::uint64_t size,align; bool operator==(const Storage&) const=default; };
enum class Kind { scalar, nominal, scalar_array, nominal_array };
struct Type { Kind kind; Storage scalar{0,0}; std::size_t owner=0; std::uint64_t count=1; };
struct Decl { std::string identity; std::vector<std::pair<std::string,Type>> fields; };
struct Field { std::uint64_t offset; Storage storage; Type type; };
struct Layout { std::string identity; Storage storage; std::vector<Field> fields; };
struct Limits { std::uint64_t size=65536,align=8; std::size_t owners=256,fields=4096; };
[[nodiscard]] inline bool power2(std::uint64_t n){return n && !(n&(n-1));}
[[nodiscard]] inline bool valid(Storage s,Limits l){
  return s.size && power2(s.align) && s.align<=l.align && s.size<=l.size && s.size%s.align==0;
}
[[nodiscard]] inline std::optional<std::uint64_t> add(std::uint64_t a,std::uint64_t b){
  if(b>std::numeric_limits<std::uint64_t>::max()-a)return std::nullopt;
  return a+b;
}
[[nodiscard]] inline std::optional<std::uint64_t> mul(std::uint64_t a,std::uint64_t b){
  if(a && b>std::numeric_limits<std::uint64_t>::max()/a)return std::nullopt;
  return a*b;
}
[[nodiscard]] inline std::optional<std::uint64_t> round(std::uint64_t n,std::uint64_t a){
  if(!power2(a))return std::nullopt;
  const auto sum=add(n,a-1);if(!sum)return std::nullopt;return *sum&~(a-1);
}
[[nodiscard]] inline bool nominal(Kind k){return k==Kind::nominal||k==Kind::nominal_array;}
[[nodiscard]] inline bool array(Kind k){return k==Kind::scalar_array||k==Kind::nominal_array;}
[[nodiscard]] inline std::optional<std::vector<Layout>> plan(const std::vector<Decl>& decls,Limits lim={}){
  if(decls.empty()||decls.size()>std::min(lim.owners,std::size_t{256})||!lim.size||!power2(lim.align))return std::nullopt;
  std::set<std::string> ids;
  std::vector<std::size_t> pending(decls.size());
  std::vector<std::vector<std::size_t>> parents(decls.size());
  std::size_t total=0;
  for(std::size_t i=0;i<decls.size();++i){
    const auto& d=decls[i];
    if(d.identity.empty()||d.identity.size()>1024||!ids.insert(d.identity).second||d.fields.empty())return std::nullopt;
    if(d.fields.size()>std::min(lim.fields,std::size_t{4096})-total)return std::nullopt;
    total+=d.fields.size();std::set<std::string> names;
    for(const auto& [name,t]:d.fields){
      if(name.empty()||name.size()>256||!names.insert(name).second)return std::nullopt;
      if(t.kind!=Kind::scalar&&t.kind!=Kind::nominal&&t.kind!=Kind::scalar_array&&t.kind!=Kind::nominal_array)return std::nullopt;
      if(!t.count||(!array(t.kind)&&t.count!=1))return std::nullopt;
      if(nominal(t.kind)){
        if(t.owner>=decls.size()||t.scalar!=Storage{0,0})return std::nullopt;
        ++pending[i];parents[t.owner].push_back(i);
      }else if(t.owner||!valid(t.scalar,lim))return std::nullopt;
    }
  }
  std::set<std::size_t> ready;
  for(std::size_t i=0;i<pending.size();++i)if(!pending[i])ready.insert(i);
  std::vector<Layout> out(decls.size());std::size_t done=0;
  while(!ready.empty()){
    const auto i=*ready.begin();ready.erase(ready.begin());
    std::uint64_t size=0,align=1;std::vector<Field> fields;
    for(const auto& [name,t]:decls[i].fields){
      (void)name;
      const auto elem=nominal(t.kind)?out[t.owner].storage:t.scalar;
      const auto bytes=mul(elem.size,t.count);if(!bytes)return std::nullopt;
      const Storage storage{*bytes,elem.align};if(!valid(storage,lim))return std::nullopt;
      const auto offset=round(size,storage.align);if(!offset)return std::nullopt;
      const auto end=add(*offset,storage.size);if(!end||*end>lim.size)return std::nullopt;
      size=*end;align=std::max(align,storage.align);fields.push_back({*offset,storage,t});
    }
    const auto bytes=round(size,align);if(!bytes||!valid({*bytes,align},lim))return std::nullopt;
    out[i]={decls[i].identity,{*bytes,align},std::move(fields)};++done;
    for(const auto p:parents[i])if(!--pending[p])ready.insert(p);
  }
  if(done!=decls.size())return std::nullopt;
  return out;
}
// Consume one index per field, then one per array element. This function
// accepts only the output of plan; never authorize ownership from an offset.
[[nodiscard]] inline std::optional<std::pair<std::uint64_t,Storage>> project(
    const std::vector<Layout>& layouts,std::size_t root,const std::vector<std::size_t>& path){
  if(root>=layouts.size()||path.empty()||path.size()>256)return std::nullopt;
  std::size_t owner=root;std::uint64_t offset=0;
  const auto finish=[&](Storage s)->std::optional<std::pair<std::uint64_t,Storage>>{
    const auto end=add(offset,s.size);
    if(!end||*end>layouts[root].storage.size)return std::nullopt;
    return std::pair{offset,s};
  };
  for(std::size_t pos=0;pos<path.size();++pos){
    if(owner>=layouts.size()||path[pos]>=layouts[owner].fields.size())return std::nullopt;
    const auto& f=layouts[owner].fields[path[pos]];
    const auto start=add(offset,f.offset);if(!start)return std::nullopt;offset=*start;
    if(pos+1==path.size())return finish(f.storage);
    if(f.type.kind==Kind::scalar)return std::nullopt;
    if(nominal(f.type.kind)&&f.type.owner>=layouts.size())return std::nullopt;
    if(array(f.type.kind)){
      const auto elem=nominal(f.type.kind)?layouts[f.type.owner].storage:f.type.scalar;
      const auto cell=path[++pos];if(cell>=f.type.count)return std::nullopt;
      const auto delta=mul(elem.size,cell);if(!delta)return std::nullopt;
      const auto end=add(*delta,elem.size);if(!end||*end>f.storage.size)return std::nullopt;
      const auto next=add(offset,*delta);if(!next)return std::nullopt;offset=*next;
      if(pos+1==path.size())return finish(elem);
      if(!nominal(f.type.kind))return std::nullopt;
    }
    owner=f.type.owner;
  }
  return std::nullopt;
}
} // namespace atto::forge::layout
