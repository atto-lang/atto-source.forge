#pragma once
// Internal resolved-call effect closure. Not guest lowering or runtime authority.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <stdexcept>
#include <vector>

namespace atto {
struct ParkingEffects {
  bool explicit_suspend{}, runtime_park{}, unresolved{};
  constexpr bool requires_resumable_frame() const { return explicit_suspend || runtime_park || unresolved; }
  constexpr bool requires_suspend_annotation() const { return explicit_suspend; }
  constexpr bool safe_with_exclusive_loan() const { return !requires_resumable_frame(); }
  constexpr bool operator==(const ParkingEffects&) const = default;
  constexpr ParkingEffects unite(ParkingEffects r) const {
    return {explicit_suspend || r.explicit_suspend, runtime_park || r.runtime_park, unresolved || r.unresolved};
  }
};
struct CallableParking {
  ParkingEffects direct;
  std::vector<std::optional<std::size_t>> calls;
};
inline std::vector<ParkingEffects> infer_parking(const std::vector<CallableParking>& nodes) {
  if (nodes.size() > 1024) throw std::invalid_argument("parking callable budget");
  std::size_t edges=0;
  for (const auto& node:nodes) {
    if (node.calls.size()>1024 || node.calls.size()>16384-edges)
      throw std::invalid_argument("parking edge budget");
    edges+=node.calls.size();
  }
  const auto n=nodes.size();
  std::vector<ParkingEffects> rows(n);
  std::vector<std::vector<std::size_t>> reverse(n);
  std::deque<std::size_t> work;
  std::vector<bool> queued(n,true);
  for(std::size_t i=0;i<n;++i) {
    rows[i]=nodes[i].direct;
    work.push_back(i);
    for(auto target:nodes[i].calls) {
      if(!target) rows[i].unresolved=true;
      else if(*target>=n) throw std::invalid_argument("invalid parking target");
      else reverse[*target].push_back(i);
    }
  }
  while(!work.empty()) {
    auto callee=work.front();work.pop_front();queued[callee]=false;
    for(auto caller:reverse[callee]) {
      auto next=rows[caller].unite(rows[callee]);
      if(next==rows[caller])continue;
      rows[caller]=next;
      if(!queued[caller]) {queued[caller]=true;work.push_back(caller);}
    }
  }
  return rows;
}
}
