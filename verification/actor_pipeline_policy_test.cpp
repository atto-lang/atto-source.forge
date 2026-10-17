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
  // Check source/retirement before validating caller-supplied route key bytes.
  try {
    (void)plan.forward_by_key("unconnected",std::string(1,char(0x80)));
    throw std::runtime_error("unknown keyed source was admitted");
  } catch(const std::invalid_argument& e) {
    check(std::string(e.what()).find("no downstream")!=std::string::npos,
          "key parsing ran before route-source validation");
  }
  // Composite names cannot collide when both sides contain delimiters.
  std::vector<PipeStage> tricky={
    {"a:b","Producer","readable","","Event"},
    {"a","Producer","readable","","Event"},
    {"c","Sink","writable","Event",""},
    {"b:c","Sink","writable","Event",""}
  };
  std::vector<PipeEdge> tricky_edges={
    {"a:b","c","Event"},{"a","b:c","Event"}};
  ActorPipePolicy separated(tricky,tricky_edges);
  check(separated.forward("a:b").target=="c","delimiter collision source one");
  check(separated.forward("a").target=="b:c","delimiter collision source two");
  deny([&]{
    auto duplicate=tricky_edges;
    duplicate.push_back(tricky_edges[0]);
    ActorPipePolicy bad(tricky,duplicate);
  });
  // UTF-8 actor instance identity is byte-exact, not Unicode normalized.
  std::vector<PipeStage> distinct=tricky;
  distinct[0].id="\xc3\xa9";
  distinct[1].id="e\xcc\x81";
  std::vector<PipeEdge> distinct_edges={
    {distinct[0].id,"c","Event"},
    {distinct[1].id,"b:c","Event"}};
  ActorPipePolicy unicode_identity(distinct,distinct_edges);
  check(unicode_identity.forward(distinct[0].id).target=="c","composed actor ID");
  check(unicode_identity.forward(distinct[1].id).target=="b:c","decomposed actor ID");
  // Reject oversized vertex and edge allocations before per-edge admission.
  deny([&]{
    auto many=tricky;
    many.resize(1025,tricky[0]);
    ActorPipePolicy bad(many,tricky_edges);
  });
  deny([&]{
    auto many=tricky_edges;
    many.resize(4097,tricky_edges[0]);
    ActorPipePolicy bad(tricky,many);
  });
  std::string exact256;
  for(int i=0;i<64;++i)exact256+="\xf0\x9f\x8c\x8d";
  auto bounded=tricky;bounded[0].id=exact256;
  auto bounded_edges=tricky_edges;bounded_edges[0].source=exact256;
  ActorPipePolicy accepted(bounded,bounded_edges);
  check(accepted.forward(exact256).target=="c","valid 256-byte actor ID");
  deny([&]{
    auto over=bounded;over[0].id+="\xf0\x9f\x8c\x8d";
    ActorPipePolicy bad(over,bounded_edges);
  });
  // A transform's declared outbound port requires a real outbound edge.
  deny([&] {
    std::vector<PipeStage> stages={
      {"src","P","readable","","Event"},
      {"terminal","T","transform","Event","Event"}
    };
    std::vector<PipeEdge> edges={{"src","terminal","Event"}};
    ActorPipePolicy invalid(stages,edges);
  });
  plan.close();deny([&]{(void)plan.forward_all("src");});
  try {
    (void)plan.forward_by_key("src",std::string(1,char(0x80)));
    throw std::runtime_error("retired keyed route was admitted");
  } catch(const std::logic_error& e) {
    check(std::string(e.what()).find("retired")!=std::string::npos,
          "key parsing ran before retirement");
  }
  ActorPipePolicy fresh(stages,edges);
  check(fresh.forward_round_robin("src").target=="a","fresh RR");
  std::cout<<"ORES_FORGE_HOST_ACTOR_PIPELINE_POLICY_PASS\n";
}
