#include "runtime/actor_pipeline_selector.hpp"
#include "compiler/frontend.hpp"
#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>
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
  // Shared RR cursor remains strict across concurrent host attempts; this
  // does not claim parallel execution of guest actor code.
  ActorPipelineSelector concurrent(stages,edges,101);
  std::atomic<int> counts[3] = {};
  std::vector<std::thread> senders;
  for (int w=0;w<8;++w) {
    senders.emplace_back([&] {
      for(int i=0;i<1000;++i) {
        auto chosen=concurrent.forward_round_robin("source").target;
        int slot = chosen=="worker-a" ? 0 : chosen=="worker-b" ? 1 :
                   chosen=="worker-c" ? 2 : -1;
        check(slot>=0,"unknown concurrent RR target");
        ++counts[slot];
      }
    });
  }
  for(auto& t:senders)t.join();
  const int a=counts[0].load(),b=counts[1].load(),c=counts[2].load();
  check(a+b+c==8000 && a==2667 && b==2667 && c==2666,
        "concurrent RR lost a turn or favored a consumer");
  concurrent.advance_generation(102,true,false);
  check(concurrent.forward_round_robin("source").target=="worker-a",
        "generation change did not reset concurrent RR");
  ActorPipelineSelector stopped(stages,edges,20);
  stopped.retire();
  rejects([&]{(void)stopped.forward("source");},"post-STOP default");
  rejects([&]{(void)stopped.forward_all("source");},"post-STOP fanout");
  rejects([&]{(void)stopped.forward_round_robin("source");},"post-STOP RR");
  rejects([&]{(void)stopped.forward_to("source","worker-a");},"post-STOP named");
  rejects([&]{(void)stopped.forward_by_key("source","alpha");},"post-STOP key");
  rejects([&]{stopped.require_ingress_root("source");},"post-STOP root");
  rejects([&]{stopped.advance_generation(21,true,false);},"post-STOP quiescent");
  rejects([&]{stopped.advance_generation(21,false,true);},"post-STOP abort");
  check(stopped.generation()==20,"STOP changed routing generation");
  stopped.retire(); // idempotent terminal state
  // Race ordinary route selection with a supervisor STOP barrier.
  // All calls that begin after retire() returns MUST fail. Calls that
  // overlap STOP may linearize on either side; never assert ordering from
  // an observation made only after a previously started operation.
  {
    ActorPipelineSelector racing(stages,edges,31);
    std::atomic<bool> begin{false}, after_stop{false};
    std::atomic<int> ready{0}, pre_attempts{0}, accepted_after{0};
    std::atomic<int> expected_rejections{0};
    std::vector<std::thread> contenders;
    for(int t=0;t<8;++t) {
      contenders.emplace_back([&,t] {
        ++ready;
        while(!begin.load(std::memory_order_acquire)) std::this_thread::yield();
        while(!after_stop.load(std::memory_order_acquire)) {
          try {
            switch(t%4) {
              case 0: (void)racing.forward_all("source"); break;
              case 1: (void)racing.forward_to("source","worker-a"); break;
              case 2: (void)racing.forward_by_key("source","k"); break;
              default: (void)racing.forward_round_robin("source"); break;
            }
          } catch(const std::invalid_argument&) { /* raced with STOP */ }
          ++pre_attempts;
        }
        for(int n=0;n<1000;++n) {
          try {
            switch(n%5) {
              case 0: (void)racing.forward_all("source"); break;
              case 1: (void)racing.forward_to("source","worker-b"); break;
              case 2: (void)racing.forward_by_key("source","beta"); break;
              case 3: (void)racing.forward_round_robin("source"); break;
              default: racing.require_ingress_root("source"); break;
            }
            ++accepted_after;
          } catch(const std::invalid_argument&) {
            ++expected_rejections;
          }
        }
      });
    }
    while(ready.load()<8) std::this_thread::yield();
    begin.store(true,std::memory_order_release);
    while(pre_attempts.load()<800) std::this_thread::yield();
    racing.retire();
    after_stop.store(true,std::memory_order_release);
    for(auto& t:contenders)t.join();
    check(accepted_after.load()==0 && expected_rejections.load()==8000,
          "a routing decision was accepted after retirement returned");
    rejects([&]{racing.advance_generation(32,true,false);},
            "retirement raced with generation reopening");
    check(racing.generation()==31,"retired selector changed generation");
  }
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
