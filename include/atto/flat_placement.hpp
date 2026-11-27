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
[[nodiscard]] inline std::optional<layout::Layout> flat_layout(const FlatPlacement& p){
  if(p.widths.empty()||p.widths.size()>8)return std::nullopt;
  layout::Decl d{"Flat",{}};
  for(auto w:p.widths){
    if(!fits_flat(w,0))return std::nullopt;
    d.fields.push_back({std::to_string(d.fields.size()),{layout::Kind::scalar,{w,w},0,1}});
  }
  auto l=layout::plan({d});if(!l)return std::nullopt;return (*l)[0];
}
[[nodiscard]] inline std::optional<std::int64_t> verify_flat_placement(const FlatPlacement& p){
  if(!flat_layout(p)||p.steps.empty()||p.steps.size()>32||p.result.empty()||p.result.size()>8)return std::nullopt;
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
