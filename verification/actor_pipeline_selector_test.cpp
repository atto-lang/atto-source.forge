#include "runtime/actor_pipeline_selector.hpp"
#include "compiler/frontend.hpp"
#include <stdexcept>
#include <string>
#include <vector>
using namespace oresforge::runtime;
static void check(bool ok, const char* reason) {
  if (!ok) throw std::runtime_error(reason);
}
template<class F> void rejects(F f, const char* why) {
  try { f(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error(why);
}
int main() {
  // No guest routing lowers here: checked selector availability must not
  // accidentally widen the Forge arithmetic-only source compiler.
  rejects([] { (void)oresforge::parse_source(
      "define readable pipe actor Producer {}"); }, "actor source accepted");
  rejects([] { (void)oresforge::parse_source(
      "fnc main():int{return self.forward(1);}"); }, "forward source accepted");
  rejects([] { (void)oresforge::parse_source(
      "fnc main():int{return self.reply(1);}"); }, "reply source accepted");
  const std::vector<PipelineStage> stages={
    {"source","Producer",PipelineRole::readable,"","Event"},
    {"worker-a","Worker",PipelineRole::writable,"Event",""},
    {"worker-b","Worker",PipelineRole::writable,"Event",""},
    {"worker-c","Worker",PipelineRole::writable,"Event",""}
  };
  const std::vector<PipelineEdge> edges={
    {"source","worker-a","Event"},{"source","worker-b","Event"},
    {"source","worker-c","Event"}
  };
  auto g=ActorPipelineSelector(stages,edges,7);
  const std::vector<std::pair<std::string,std::size_t>> vectors={
    {"customer-7",0},{"alpha",0},{"beta",2},{"\xf0\x9f\x8c\x8d",0},{"",2},{std::string(1,'\0'),0}
  };
  for (const auto& [key,slot]:vectors) {
    check(pipeline_key_slot(key,3)==slot,"FNV-1a cross-backend vector");
    check(g.forward_by_key("source",key).target==edges.at(slot).target,
          "keyed actor affinity mismatch");
  }
  check(g.forward_all("source").size()==3,"fan-out");
  rejects([&]{(void)g.forward("source");},"ambiguous default forward");
  check(g.forward_to("source","worker-b").target=="worker-b","named edge");
  rejects([&]{(void)g.forward_to("source","worker-z");},"disconnected named edge");
  g.require_ingress_root("source");
  rejects([&]{g.require_ingress_root("worker-a");},"internal worker ingress");
  for(int i=0;i<999;++i)
    check(g.forward_round_robin("source").target==edges.at(i%3).target,
          "round-robin publication order");
  rejects([&]{g.advance_generation(8,false,false);},"unfenced epoch");
  g.advance_generation(8,true,false);
  check(g.forward_round_robin("source").target=="worker-a","epoch reset");
  rejects([&]{g.advance_generation(8,true,false);},"same epoch");
  rejects([&]{(void)pipeline_key_slot("\xc0\xaf",3);},"overlong route key");
  rejects([&]{(void)pipeline_key_slot("\xed\xa0\x80",3);},"surrogate route key");
  rejects([&]{(void)pipeline_key_slot(std::string(4097,'x'),3);},"oversize key");
  rejects([&]{(void)pipeline_key_slot("k",0);},"zero downstream");
  rejects([&]{
    auto wrong=stages; wrong.push_back(stages[1]);
    ActorPipelineSelector p(wrong,edges);
  },"duplicate stage instance");
  rejects([&]{
    auto wrong=stages; wrong[1].id=std::string("\0z",2);
    ActorPipelineSelector p(wrong,edges);
  },"NUL instance");
  rejects([&]{
    auto wrong=stages; wrong[1].id=std::string(257,'z');
    ActorPipelineSelector p(wrong,edges);
  },"oversize stage");
  rejects([&]{
    auto wrong=edges; wrong.push_back(edges[0]);
    ActorPipelineSelector p(stages,wrong);
  },"duplicate edge");
  rejects([&]{
    auto wrong=edges; wrong[0].payload="Different";
    ActorPipelineSelector p(stages,wrong);
  },"nominal payload mismatch");
  rejects([&]{
    std::vector<PipelineStage> nodes={
      {"root","Root",PipelineRole::readable,"","X"},
      {"a","Node",PipelineRole::duplex,"X","X"},
      {"b","Node",PipelineRole::duplex,"X","X"}
    };
    std::vector<PipelineEdge> cycle={{"root","a","X"},{"a","b","X"},{"b","a","X"}};
    ActorPipelineSelector p(nodes,cycle);
  },"cycle");
}
