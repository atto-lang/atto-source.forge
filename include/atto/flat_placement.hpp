#pragma once
#include "atto/physical_layout.hpp"
#include "atto/checked_i64.hpp"
#include <cstdint>
#include <optional>
#include <vector>
namespace atto::forge::scalar {
struct FlatPlacement {
  enum class Kind { construct, bind, copy_field, detach, destroy, free };
  struct Step { Kind kind; std::vector<std::int64_t> values{}; std::size_t field=0; };
  std::vector<std::uint64_t> widths;
  std::vector<layout::Decl> owners;
  std::vector<Step> steps;
  std::vector<std::size_t> result;
};
[[nodiscard]] inline bool fits_flat(std::uint64_t width,std::int64_t v){
  switch(width){
    case 1:return v>=INT8_MIN&&v<=INT8_MAX;
    case 2:return v>=INT16_MIN&&v<=INT16_MAX;
    case 4:return v>=INT32_MIN&&v<=INT32_MAX;
    case 8:return true;
    default:return false;
  }
}
struct FlatLeaf {std::uint64_t width;std::vector<std::size_t> path;};
struct FlatShape {std::vector<layout::Layout> layouts;std::vector<FlatLeaf> leaves;};
[[nodiscard]] inline std::optional<FlatShape> flat_shape(const FlatPlacement& p){
  if(p.owners.empty()||p.owners.size()>8)return std::nullopt;
  auto layouts=layout::plan(p.owners);if(!layouts)return std::nullopt;
  std::vector<FlatLeaf> leaves;std::vector<std::size_t> path;
  const auto visit=[&](auto&& self,std::size_t owner)->bool{
    if(path.size()>=8||owner>=p.owners.size())return false;
    const auto& fields=p.owners[owner].fields;
    for(std::size_t i=0;i<fields.size();++i){
      const auto& ty=fields[i].second;path.push_back(i);
      if(ty.kind==layout::Kind::scalar){
        if(!fits_flat(ty.scalar.size,0)||ty.scalar.size!=ty.scalar.align||leaves.size()>=8)return false;
        leaves.push_back({ty.scalar.size,path});
      }else if(ty.kind==layout::Kind::nominal){
        if(ty.owner>=owner||!self(self,ty.owner))return false;
      }else return false;
      path.pop_back();
    }
    return true;
  };
  if(!visit(visit,p.owners.size()-1)||leaves.size()!=p.widths.size())return std::nullopt;
  for(std::size_t i=0;i<leaves.size();++i)if(leaves[i].width!=p.widths[i])return std::nullopt;
  // Validate even declarations not reachable from the last owner.
  for(std::size_t i=0;i<p.owners.size();++i)for(const auto& [name,ty]:p.owners[i].fields){
    (void)name;
    if(ty.kind==layout::Kind::nominal){if(ty.owner>=i)return std::nullopt;}
    else if(ty.kind!=layout::Kind::scalar||!fits_flat(ty.scalar.size,0)||ty.scalar.size!=ty.scalar.align)return std::nullopt;
  }
  return FlatShape{std::move(*layouts),std::move(leaves)};
}
[[nodiscard]] inline std::optional<std::int64_t> verify_flat_placement(const FlatPlacement& p){
  if(!flat_shape(p)||p.steps.empty()||p.steps.size()>32||p.result.empty()||p.result.size()>8)return std::nullopt;
  enum class State{empty,live,freed};State state=State::empty;
  unsigned generation=0;std::optional<unsigned> bound;
  std::vector<std::int64_t> fields,values;
  using K=FlatPlacement::Kind;
  for(const auto& s:p.steps){
    if(s.kind!=K::construct&&!s.values.empty())return std::nullopt;
    if(s.kind!=K::copy_field&&s.field)return std::nullopt;
    switch(s.kind){
      case K::construct:
        if(state!=State::empty||bound||s.values.size()!=p.widths.size())return std::nullopt;
        for(std::size_t i=0;i<p.widths.size();++i)if(!fits_flat(p.widths[i],s.values[i]))return std::nullopt;
        fields=s.values;++generation;state=State::live;break;
      case K::bind:if(state!=State::live||bound)return std::nullopt;bound=generation;break;
      case K::copy_field:
        if(state!=State::live||!bound||*bound!=generation||s.field>=fields.size())return std::nullopt;
        values.push_back(fields[s.field]);break;
      case K::detach:if(state!=State::live||!bound||*bound!=generation)return std::nullopt;bound.reset();break;
      case K::destroy:if(state!=State::live||bound)return std::nullopt;state=State::empty;break;
      case K::free:if(state!=State::empty||bound)return std::nullopt;state=State::freed;break;
      default:return std::nullopt;
    }
  }
  if(state!=State::freed||bound)return std::nullopt;
  std::int64_t sum=0;
  for(auto i:p.result){if(i>=values.size())return std::nullopt;auto v=atto::forge::checked_add(sum,values[i]);if(!v)return std::nullopt;sum=*v;}
  return sum;
}
}
