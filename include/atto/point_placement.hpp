#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace atto::forge::scalar {
// Independently replayable nominal two-i64 Point construction profile.
// There are no public runtime addresses or source-supplied layout offsets.
struct PointPlacement {
  enum class Kind { construct, bind, copy_field, detach, destroy, free };
  struct Step { Kind kind; std::int64_t x=0,y=0; std::size_t field=0; };
  std::vector<Step> steps;
  std::vector<std::size_t> result;
};
[[nodiscard]] inline std::optional<std::int64_t> verify_point_placement(const PointPlacement& p) {
  if(p.steps.empty()||p.steps.size()>32||p.result.empty()||p.result.size()>2) return std::nullopt;
  enum class State { empty, live, freed };
  State state=State::empty;
  unsigned generation=0;
  std::optional<unsigned> bound;
  std::array<std::int64_t,2> fields{};
  std::vector<std::int64_t> values;
  for(const auto& s:p.steps){
    if(s.kind!=PointPlacement::Kind::construct&&(s.x||s.y)) return std::nullopt;
    if(s.kind!=PointPlacement::Kind::copy_field&&s.field) return std::nullopt;
    switch(s.kind){
      case PointPlacement::Kind::construct:
        if(state!=State::empty||bound||s.x<0||s.y<0)return std::nullopt;
        fields={s.x,s.y};++generation;state=State::live;break;
      case PointPlacement::Kind::bind:
        if(state!=State::live||bound)return std::nullopt;
        bound=generation;break;
      case PointPlacement::Kind::copy_field:
        if(state!=State::live||!bound||*bound!=generation||s.field>=2)return std::nullopt;
        values.push_back(fields[s.field]);break;
      case PointPlacement::Kind::detach:
        if(state!=State::live||!bound||*bound!=generation)return std::nullopt;
        bound.reset();break;
      case PointPlacement::Kind::destroy:
        if(state!=State::live||bound)return std::nullopt;
        state=State::empty;break;
      case PointPlacement::Kind::free:
        if(state!=State::empty||bound)return std::nullopt;
        state=State::freed;break;
      default:return std::nullopt;
    }
  }
  if(state!=State::freed||bound)return std::nullopt;
  std::int64_t answer=0;
  for(auto i:p.result){
    if(i>=values.size()||answer>std::numeric_limits<std::int64_t>::max()-values[i])return std::nullopt;
    answer+=values[i];
  }
  return answer;
}
} // namespace atto::forge::scalar
