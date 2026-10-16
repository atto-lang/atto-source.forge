#pragma once
// Host-only actor routing selector v1. No guest source lowering, message
// admission, actor authority, or correlated backward/reply delivery.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace oresforge::runtime {
enum class PipelineRole { readable, transform, writable, duplex };
struct PipelineStage {
  std::string id, actor_type;
  PipelineRole role;
  std::string inbound, outbound;
};
struct PipelineEdge { std::string source, target, payload; };

inline bool valid_pipe_utf8(std::string_view value,
                            std::size_t maximum, bool optional=false,
                            bool forbid_nul=true) noexcept {
  if ((value.empty() && !optional) || value.size()>maximum) return false;
  for (std::size_t i=0; i<value.size();) {
    const auto a=static_cast<unsigned char>(value[i]);
    if (a==0 && forbid_nul) return false;
    if (a<=0x7f) { ++i; continue; }
    std::size_t width=0;
    if (a>=0xc2 && a<=0xdf) width=2;
    else if (a>=0xe0 && a<=0xef) width=3;
    else if (a>=0xf0 && a<=0xf4) width=4;
    else return false;
    if (width>value.size()-i) return false;
    for (std::size_t j=1; j<width; ++j) {
      const auto b=static_cast<unsigned char>(value[i+j]);
      if ((b&0xc0)!=0x80) return false;
    }
    const auto b=static_cast<unsigned char>(value[i+1]);
    if ((a==0xe0 && b<0xa0) || (a==0xed && b>0x9f) ||
        (a==0xf0 && b<0x90) || (a==0xf4 && b>0x8f)) return false;
    i+=width;
  }
  return true;
}
inline std::size_t pipeline_key_slot(std::string_view key, std::size_t count) {
  if (count==0 || count>4096 || !valid_pipe_utf8(key,4096,true,false))
    throw std::invalid_argument("invalid route key or downstream count");
  std::uint64_t hash=UINT64_C(14695981039346656037);
  for (const unsigned char b : key) {
    hash^=b;
    hash*=UINT64_C(1099511628211);
  }
  return static_cast<std::size_t>(hash % count);
}
class ActorPipelineSelector final {
 public:
  ActorPipelineSelector(const std::vector<PipelineStage>& stages,
                        const std::vector<PipelineEdge>& edges,
                        std::uint64_t generation=1)
      : generation_(generation) {
    if (generation==0 || stages.size()<2 || stages.size()>1024 ||
        edges.empty() || edges.size()>4096)
      throw std::invalid_argument("invalid pipeline graph budget");
    std::unordered_map<std::string,PipelineStage> ids;
    std::unordered_map<std::string,std::size_t> incoming;
    for (const auto& stage:stages) {
      if (!valid_pipe_utf8(stage.id,256) ||
          !valid_pipe_utf8(stage.actor_type,256) ||
          !valid_pipe_utf8(stage.inbound,256,true) ||
          !valid_pipe_utf8(stage.outbound,256,true) ||
          !ids.emplace(stage.id,stage).second)
        throw std::invalid_argument("duplicate or malformed pipeline stage");
      const bool in=!stage.inbound.empty(), out=!stage.outbound.empty();
      const bool ports = (stage.role==PipelineRole::readable && !in && out) ||
        (stage.role==PipelineRole::writable && in && !out) ||
        ((stage.role==PipelineRole::transform || stage.role==PipelineRole::duplex)
          && in && out);
      if (!ports) throw std::invalid_argument("invalid pipeline stage ports");
      incoming.emplace(stage.id,0);
      outgoing_.emplace(stage.id,std::vector<PipelineEdge>{});
    }
    std::unordered_set<std::string> pairs;
    for (const auto& edge:edges) {
      if (!valid_pipe_utf8(edge.source,256) || !valid_pipe_utf8(edge.target,256) ||
          !valid_pipe_utf8(edge.payload,256))
        throw std::invalid_argument("invalid pipeline link component");
      const auto s=ids.find(edge.source), t=ids.find(edge.target);
      if (s==ids.end() || t==ids.end() || s==t ||
          s->second.role==PipelineRole::writable ||
          t->second.role==PipelineRole::readable ||
          edge.payload!=s->second.outbound || edge.payload!=t->second.inbound)
        throw std::invalid_argument("invalid or mistyped pipeline link");
      // Length prefixes prevent delimiter collisions in instance IDs.
      const auto pair=std::to_string(edge.source.size())+":"+edge.source+
                      std::to_string(edge.target.size())+":"+edge.target;
      if (!pairs.insert(pair).second)
        throw std::invalid_argument("duplicate pipeline connection");
      outgoing_.at(edge.source).push_back(edge);
      ++incoming.at(edge.target);
    }
    std::deque<std::string> ready;
    for (const auto& stage:stages) {
      const auto r=incoming.at(stage.id),w=outgoing_.at(stage.id).size();
      if ((!r && !w) || (stage.role==PipelineRole::readable && !w) ||
          (stage.role==PipelineRole::writable && !r) ||
          (stage.role==PipelineRole::transform && (!r || !w)))
        throw std::invalid_argument("pipeline stage lacks required port");
      if (r==0) {
        ready.push_back(stage.id);
        if (w && (stage.role==PipelineRole::readable ||
                  stage.role==PipelineRole::duplex)) roots_.insert(stage.id);
      }
    }
    if (roots_.empty()) throw std::invalid_argument("pipeline lacks ingress root");
    std::size_t visited=0;
    while (!ready.empty()) {
      auto current=std::move(ready.front()); ready.pop_front(); ++visited;
      for (const auto& edge:outgoing_.at(current))
        if (--incoming.at(edge.target)==0) ready.push_back(edge.target);
    }
    if (visited!=stages.size())
      throw std::invalid_argument("pipeline graph contains cycle");
  }
  // Retirement is linearizable with ALL host-only selection operations.
  // A selector whose retirement returns can never issue another route.
  // Routing references remain immutable host metadata, NOT a guest bearer
  // authority; actual mailbox admission still needs scheduler authentication.
  void retire() {
    std::lock_guard<std::mutex> guard(mutex_);
    retired_=true;
  }
  void require_ingress_root(const std::string& source) const {
    std::lock_guard<std::mutex> guard(mutex_);
    ensure_active_locked();
    if (!roots_.contains(source))
      throw std::invalid_argument("not an ingress root");
  }
  std::vector<PipelineEdge> forward_all(const std::string& source) const {
    std::lock_guard<std::mutex> guard(mutex_);
    // Return owned routing metadata: callers cannot retain a view into a
    // selector whose lifetime/retirement they do not control.
    return connected_locked(source);
  }
  PipelineEdge forward(const std::string& source) const {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto& edges=connected_locked(source);
    if (edges.size()!=1) throw std::invalid_argument("ambiguous pipeline forward");
    return edges.front();
  }
  PipelineEdge forward_round_robin(const std::string& source) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto& edges=connected_locked(source);
    auto& cursor=cursors_[source];
    PipelineEdge selected=edges.at(cursor);
    cursor=(cursor+1)%edges.size();
    return selected;
  }
  PipelineEdge forward_to(const std::string& source,
                                 const std::string& target) const {
    std::lock_guard<std::mutex> guard(mutex_);
    for (const auto& edge:connected_locked(source))
      if (edge.target==target) return edge;
    throw std::invalid_argument("pipeline destination not connected");
  }
  PipelineEdge forward_by_key(const std::string& source,
                                     std::string_view key) const {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto& edges=connected_locked(source);
    return edges.at(pipeline_key_slot(key,edges.size()));
  }
  void advance_generation(std::uint64_t successor, bool quiescent, bool aborted) {
    std::lock_guard<std::mutex> guard(mutex_);
    ensure_active_locked();
    if (successor<=generation_ || (!quiescent && !aborted))
      throw std::invalid_argument("pipeline generation barrier required");
    generation_=successor;
    cursors_.clear();
  }
  std::uint64_t generation() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return generation_;
  }
 private:
  void ensure_active_locked() const {
    if (retired_) throw std::invalid_argument("pipeline selector retired");
  }
  const std::vector<PipelineEdge>& connected_locked(
      const std::string& source) const {
    ensure_active_locked();
    const auto it=outgoing_.find(source);
    if (it==outgoing_.end() || it->second.empty())
      throw std::invalid_argument("source has no downstream");
    return it->second;
  }
  std::unordered_map<std::string,std::vector<PipelineEdge>> outgoing_;
  std::unordered_set<std::string> roots_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string,std::size_t> cursors_;
  std::uint64_t generation_;
  bool retired_=false;
};
}  // namespace oresforge::runtime
