#include "runtime/actor_pipeline_policy.hpp"
#include <stdexcept>
#include <functional>
#include <iostream>
#include <tuple>
using namespace oresforge::runtime;
static void check(bool ok,const char* why){
  if(!ok)throw std::runtime_error(why);
}
static void deny(const std::function<void()>& fn){
  bool failed=false;
  try{fn();}catch(const std::exception&){failed=true;}
  check(failed,"missing expected rejection");
}
int main(){
  auto stages=std::vector<PipeStage>{{"src","Producer","readable","","Event"},
    {"a","Worker","writable","Event",""},{"b","Worker","writable","Event",""},
    {"c","Worker","writable","Event",""}};
  auto edges=std::vector<PipeEdge>{{"src","a","Event"},{"src","b","Event"},{"src","c","Event"}};
  ActorPipePolicy plan(stages,edges);
  check(plan.forward_all("src").size()==3,"fanout");
  for(const char* expected:{"a","b","c","a","b","c"})
    check(plan.forward_round_robin("src").target==expected,"round-robin");
  deny([&]{(void)plan.forward("src");});
  check(plan.forward_to("src","b").target=="b","target");
  deny([&]{(void)plan.forward_to("src","z");});
  for(const auto& [key,target,hash]:std::vector<std::tuple<std::string,std::string,std::uint64_t>>{
     {"customer-7","a",UINT64_C(0x660308ab83c6fdf1)},
     {"alpha","a",UINT64_C(0x8ac625bb85ed202b)},
     {"beta","c",UINT64_C(0x7627619b954620a7)},
     {"\xf0\x9f\x8c\x8d","a",UINT64_C(0xff430438753bca3b)},
     {"","c",UINT64_C(0xcbf29ce484222325)}}){
    check(pipe_route_hash64(key)==hash,"hash");
    check(plan.forward_by_key("src",key).target==target,"key-route");
  }
  deny([&]{auto bad=edges;bad.push_back(edges.front());ActorPipePolicy x(stages,bad);});
  deny([&]{auto bad=edges;bad.front().payload="Wrong";ActorPipePolicy x(stages,bad);});
  deny([&]{auto bad=stages;bad[1].id="src";ActorPipePolicy x(bad,edges);});
  deny([&]{auto bad=stages;bad[0].id=std::string(257,'x');ActorPipePolicy x(bad,edges);});
  deny([&]{auto bad=stages;bad[0].id=std::string(1,'\0');ActorPipePolicy x(bad,edges);});
  deny([&]{(void)pipe_route_hash64(std::string("\xc0\xaf",2));});
  deny([&]{(void)pipe_route_hash64(std::string(4097,'x'));});
  plan.close();deny([&]{(void)plan.forward_all("src");});
  ActorPipePolicy fresh(stages,edges);
  check(fresh.forward_round_robin("src").target=="a","fresh RR");
  std::cout<<"ORES_FORGE_HOST_ACTOR_PIPELINE_POLICY_PASS\n";
}
