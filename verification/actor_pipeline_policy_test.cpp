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
     {"","c",UINT64_C(0xcbf29ce484222325)},
     {std::string(1, '\0'),"a",UINT64_C(0xaf63bd4c8601b7df)}}){
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
  // Round-robin selection is exactly fair over long sequences, not only
  // the first complete sweep. Existing read/write mailboxes are not involved.
  std::size_t counts[3]={0,0,0};
  for(int i=0;i<12000;++i){
    auto selected=plan.forward_round_robin("src").target;
    if(selected=="a")++counts[0];
    else if(selected=="b")++counts[1];
    else if(selected=="c")++counts[2];
    else throw std::runtime_error("round-robin chose unknown target");
  }
  check(counts[0]==4000&&counts[1]==4000&&counts[2]==4000,
        "12k round-robin fairness");
  // Instance type is nominal; repeated Worker classes have independent IDs.
  std::vector<PipeStage> diamond={
    {"root","Producer","readable","","Event"},
    {"branch-a","Worker","transform","Event","Event"},
    {"branch-b","Worker","transform","Event","Event"},
    {"sink","Sink","writable","Event",""}
  };
  std::vector<PipeEdge> diamond_edges={
    {"root","branch-a","Event"},{"root","branch-b","Event"},
    {"branch-a","sink","Event"},{"branch-b","sink","Event"}
  };
  ActorPipePolicy diamond_plan(diamond,diamond_edges);
  check(diamond_plan.forward_all("root").size()==2,"valid diamond fan-in");
  deny([&] {
    auto cyclic=diamond_edges;
    cyclic.push_back({"branch-a","branch-b","Event"});
    cyclic.push_back({"branch-b","branch-a","Event"});
    ActorPipePolicy bad(diamond,cyclic);
  });
  deny([&] {
    auto self=diamond_edges;self.push_back({"branch-a","branch-a","Event"});
    ActorPipePolicy bad(diamond,self);
  });
  auto long_unicode=std::string{};
  for(int i=0;i<128;++i)long_unicode+="\xc3\xa9";
  auto unicode=stages;unicode[0].id=long_unicode;
  auto unicode_edges=edges;
  for(auto& edge:unicode_edges)edge.source=long_unicode;
  ActorPipePolicy unicode_ok(unicode,unicode_edges);
  check(unicode_ok.forward_all(long_unicode).size()==3,
        "valid 256-byte Unicode ID");
  deny([&] {
    auto too_long=unicode;too_long[0].id+="\xc3\xa9";
    ActorPipePolicy bad(too_long,unicode_edges);
  });
  deny([&] { (void)pipe_route_hash64(std::string("\xed\xa0\x80",3)); });
  deny([&] { (void)pipe_route_hash64(std::string("\xf4\x90\x80\x80",4)); });
  deny([&] { (void)pipe_route_hash64(std::string("\xe2\x82",2)); });
  auto valid_key=std::string{};
  for(int i=0;i<1024;++i)valid_key+="\xf0\x9f\x8c\x8d";
  (void)pipe_route_hash64(valid_key);
  deny([&] { (void)pipe_route_hash64(valid_key+"\xf0\x9f\x8c\x8d"); });
  plan.close();deny([&]{(void)plan.forward_all("src");});
  ActorPipePolicy fresh(stages,edges);
  check(fresh.forward_round_robin("src").target=="a","fresh RR");
  std::cout<<"ORES_FORGE_HOST_ACTOR_PIPELINE_POLICY_PASS\n";
}
