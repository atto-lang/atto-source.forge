#pragma once
// Trusted C++20 graph selector. NO guest self.* lowering or actor authority.
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
struct PipeStage { std::string id,type,role,inbound,outbound; };
struct PipeEdge { std::string source,target,payload; };
inline bool valid_pipe_string(std::string_view s,bool optional=false) {
  if(s.empty())return optional;
  if(s.size()>256)return false;
  for(std::size_t i=0;i<s.size();) {
    unsigned char c=static_cast<unsigned char>(s[i]);
    if(c==0)return false;
    if(c<=0x7f){++i;continue;}
    std::size_t n=0;unsigned char lo=0x80,hi=0xbf;
    if(c>=0xc2&&c<=0xdf)n=1;
    else if(c>=0xe0&&c<=0xef){n=2;if(c==0xe0)lo=0xa0;if(c==0xed)hi=0x9f;}
    else if(c>=0xf0&&c<=0xf4){n=3;if(c==0xf0)lo=0x90;if(c==0xf4)hi=0x8f;}
    else return false;
    if(n>=s.size()-i)return false;
    for(std::size_t j=1;j<=n;++j){
      auto b=static_cast<unsigned char>(s[i+j]);
      if(b<(j==1?lo:0x80)||b>(j==1?hi:0xbf))return false;
    }
    i+=n+1;
  }
  return true;
}
inline std::uint64_t pipe_route_hash64(std::string_view s) {
  if(s.size()>4096)throw std::invalid_argument("routing key byte limit");
  // A route key may be >256 bytes; validate scalar encoding separately.
  for(std::size_t i=0;i<s.size();) {
    auto a=static_cast<unsigned char>(s[i]);
    if(a<=0x7f){++i;continue;}
    std::size_t n=(a>=0xc2&&a<=0xdf)?2:(a>=0xe0&&a<=0xef)?3:
                  (a>=0xf0&&a<=0xf4)?4:0;
    if(n==0||n>s.size()-i)throw std::invalid_argument("invalid UTF-8 routing key");
    auto b=static_cast<unsigned char>(s[i+1]);
    if((b&0xc0)!=0x80||(a==0xe0&&b<0xa0)||(a==0xed&&b>0x9f)||
       (a==0xf0&&b<0x90)||(a==0xf4&&b>0x8f))
      throw std::invalid_argument("invalid UTF-8 routing key");
    for(std::size_t j=2;j<n;++j)
      if((static_cast<unsigned char>(s[i+j])&0xc0)!=0x80)
        throw std::invalid_argument("invalid UTF-8 routing key");
    i+=n;
  }
  std::uint64_t hash=UINT64_C(14695981039346656037);
  for(unsigned char c:s){hash^=c;hash*=UINT64_C(1099511628211);}
  return hash;
}
class ActorPipePolicy {
  std::vector<PipeEdge> edges_;
  std::unordered_map<std::string,std::vector<std::size_t>> outgoing_;
  std::unordered_map<std::string,std::size_t> cursor_;
  std::mutex gate_;
  bool closed_=false;
  const std::vector<std::size_t>& connected(const std::string& source)const {
    if(closed_)throw std::logic_error("pipeline generation retired");
    auto it=outgoing_.find(source);
    if(it==outgoing_.end()||it->second.empty())
      throw std::invalid_argument("no downstream connection");
    return it->second;
  }
 public:
  ActorPipePolicy(std::vector<PipeStage> stages,std::vector<PipeEdge> edges)
      :edges_(std::move(edges)) {
    if(stages.size()<2||stages.size()>1024||edges_.empty()||edges_.size()>4096)
      throw std::invalid_argument("pipeline graph budget");
    std::unordered_map<std::string,std::size_t> ids;
    for(std::size_t i=0;i<stages.size();++i){
      const auto& s=stages[i];
      if(!valid_pipe_string(s.id)||!valid_pipe_string(s.type)||
         !valid_pipe_string(s.inbound,true)||!valid_pipe_string(s.outbound,true)||
         !ids.emplace(s.id,i).second)
        throw std::invalid_argument("invalid pipeline instance");
      bool valid=(s.role=="readable"&&s.inbound.empty()&&!s.outbound.empty())||
        (s.role=="writable"&&s.outbound.empty()&&!s.inbound.empty())||
        ((s.role=="transform"||s.role=="duplex")&&
         !s.inbound.empty()&&!s.outbound.empty());
      if(!valid)throw std::invalid_argument("invalid typed actor role");
    }
    std::vector<std::size_t> indegree(stages.size()),in(stages.size()),out(stages.size());
    std::unordered_set<std::string> pairs;
    for(std::size_t i=0;i<edges_.size();++i){
      const auto& e=edges_[i];
      if(!valid_pipe_string(e.source)||!valid_pipe_string(e.target)||
         !valid_pipe_string(e.payload))
        throw std::invalid_argument("invalid graph edge");
      auto a=ids.find(e.source),b=ids.find(e.target);
      if(a==ids.end()||b==ids.end()||a->second==b->second)
        throw std::invalid_argument("unknown/self pipeline endpoint");
      const auto& from=stages[a->second];const auto& to=stages[b->second];
      if(from.role=="writable"||to.role=="readable"||
         e.payload!=from.outbound||e.payload!=to.inbound)
        throw std::invalid_argument("nominal edge mismatch");
      if(!pairs.insert(std::to_string(a->second)+":"+std::to_string(b->second)).second)
        throw std::invalid_argument("duplicate pipeline edge");
      outgoing_[e.source].push_back(i);
      ++out[a->second];++in[b->second];++indegree[b->second];
    }
    std::deque<std::size_t> ready;
    bool root=false;
    for(std::size_t i=0;i<stages.size();++i){
      const auto& role=stages[i].role;
      if((in[i]==0&&out[i]==0)||(role=="readable"&&out[i]==0)||
         (role=="writable"&&in[i]==0)||
         (role=="transform"&&(in[i]==0||out[i]==0)))
        throw std::invalid_argument("missing typed actor port");
      if(indegree[i]==0){
        ready.push_back(i);
        if(role=="readable"||role=="duplex")root=true;
      }
    }
    if(!root)throw std::invalid_argument("no ingress root");
    std::size_t visited=0;
    while(!ready.empty()){
      auto next=ready.front();ready.pop_front();++visited;
      for(auto edgeId:outgoing_[stages[next].id]){
        auto target=ids.at(edges_[edgeId].target);
        if(--indegree[target]==0)ready.push_back(target);
      }
    }
    if(visited!=stages.size())throw std::invalid_argument("pipeline graph cycle");
  }
  std::vector<PipeEdge> forward_all(const std::string& source){
    std::scoped_lock lock(gate_);
    std::vector<PipeEdge> result;
    for(auto i:connected(source))result.push_back(edges_[i]);
    return result;
  }
  PipeEdge forward(const std::string& source){
    std::scoped_lock lock(gate_);
    auto& choices=connected(source);
    if(choices.size()!=1)throw std::invalid_argument("ambiguous forward");
    return edges_[choices.front()];
  }
  PipeEdge forward_round_robin(const std::string& source){
    std::scoped_lock lock(gate_);
    auto& choices=connected(source);
    auto i=cursor_[source];
    cursor_[source]=(i+1)%choices.size();
    return edges_[choices[i]];
  }
  PipeEdge forward_to(const std::string& source,const std::string& target){
    std::scoped_lock lock(gate_);
    for(auto i:connected(source))if(edges_[i].target==target)return edges_[i];
    throw std::invalid_argument("target is not connected");
  }
  PipeEdge forward_by_key(const std::string& source,std::string_view key){
    auto hash=pipe_route_hash64(key);
    std::scoped_lock lock(gate_);
    auto& choices=connected(source);
    return edges_[choices[hash%choices.size()]];
  }
  void close(){std::scoped_lock lock(gate_);closed_=true;}
};
}
