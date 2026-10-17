#include "runtime/actor_pipeline_policy.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace oresforge::runtime;

static ActorPipePolicy make_plan() {
  return ActorPipePolicy(
    {{"src","Producer","readable","","Event"},
     {"a","Worker","writable","Event",""},
     {"b","Worker","writable","Event",""},
     {"c","Worker","writable","Event",""}},
    {{"src","a","Event"},{"src","b","Event"},{"src","c","Event"}});
}
int main() {
  // All 24,000 selections must be accounted for exactly once. This proves
  // selector synchronization, not parallel execution of Oreslang actor code.
  auto plan=make_plan();
  std::array<std::atomic<int>,3> counts{};
  std::atomic<bool> go=false;
  std::atomic<int> invalid=0;
  std::vector<std::thread> threads;
  for(int i=0;i<8;i++){
    threads.emplace_back([&]{
      while(!go.load(std::memory_order_acquire))std::this_thread::yield();
      for(int n=0;n<3000;n++) {
        try{
          auto target=plan.forward_round_robin("src").target;
          if(target=="a")counts[0].fetch_add(1);
          else if(target=="b")counts[1].fetch_add(1);
          else if(target=="c")counts[2].fetch_add(1);
          else invalid.fetch_add(1);
        }catch(...){invalid.fetch_add(1);}
      }
    });
  }
  go.store(true,std::memory_order_release);
  for(auto& thread:threads)thread.join();
  if(invalid.load()!=0||counts[0].load()!=8000||
     counts[1].load()!=8000||counts[2].load()!=8000)
    throw std::runtime_error("concurrent round-robin lost or duplicated selections");

  // Retirement establishes a synchronized boundary: operations that begin
  // after close returns cannot silently route to any consumer.
  auto closing=make_plan();
  std::atomic<bool> retired=false;
  std::atomic<int> admitted_after_close=0;
  std::atomic<int> attempts_after_close=0;
  threads.clear();
  for(int i=0;i<8;i++){
    threads.emplace_back([&]{
      for(int n=0;n<4000;n++){
        bool started_after=retired.load(std::memory_order_acquire);
        if(started_after)attempts_after_close.fetch_add(1);
        try {
          (void)closing.forward_round_robin("src");
          if(started_after)admitted_after_close.fetch_add(1);
        }catch(const std::logic_error&){
          // Once retired, this is the required error.
        }
      }
    });
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  closing.close();
  retired.store(true,std::memory_order_release);
  for(auto& thread:threads)thread.join();
  // Force at least one post-retirement attempt even on very fast hosts.
  try {
    (void)closing.forward_round_robin("src");
    admitted_after_close.fetch_add(1);
  }catch(const std::logic_error&){}
  if(admitted_after_close.load()!=0)
    throw std::runtime_error("retired pipeline accepted a new selection");

  std::cout<<"ORES_FORGE_ACTOR_PIPELINE_CONCURRENCY_PASS"
           <<" concurrent=24000 retirement_attempts=32000\n";
}
