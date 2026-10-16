#include "runtime/actor.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using oresforge::runtime::Mailbox;
int main() {
 try {
  constexpr int P=4,C=3,N=1500,T=P*N;
  Mailbox box(901,128,1024);
  auto ref=box.ref();
  std::vector<std::atomic<int>> seen(T);
  for(auto& a:seen)a=0;
  std::atomic<int> done{0},received{0};
  std::atomic<bool> failed{false};
  const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(20);
  std::vector<std::thread> readers,writers;
  for(int c=0;c<C;++c)readers.emplace_back([&]{
   while(!failed && (done<P || received<T)) {
    if(std::chrono::steady_clock::now()>until){failed=true;break;}
    auto v=box.receive();
    if(!v){std::this_thread::yield();continue;}
    if(v->size()!=4){failed=true;break;}
    std::uint32_t id=0;
    for(int b=0;b<4;++b)id|=std::uint32_t((*v)[b])<<(8*b);
    if(id>=T || seen[id].fetch_add(1)!=0){failed=true;break;}
    ++received;
   }
  });
  for(int p=0;p<P;++p)writers.emplace_back([&,p]{
   for(int n=0;n<N && !failed;++n){
    const std::uint32_t id=p*N+n;
    std::array<std::uint8_t,4> bytes{};
    for(int b=0;b<4;++b)bytes[b]=std::uint8_t(id>>(8*b));
    while(!failed){
     if(std::chrono::steady_clock::now()>until){failed=true;break;}
     try{box.send(ref,bytes);break;}
     catch(const std::length_error&){std::this_thread::yield();}
     catch(const std::exception&){failed=true;break;}
    }
   }
   ++done;
  });
  for(auto& t:writers)t.join();
  for(auto& t:readers)t.join();
  if(failed || received!=T || box.queued()!=0)throw std::runtime_error("MPMC count/progress");
  for(auto& a:seen)if(a!=1)throw std::runtime_error("MPMC duplicate/loss");
  const auto stale=box.ref();
  std::atomic<bool> stop{false};
  std::thread contender([&]{
   while(!stop){
    try{auto r=box.ref();box.send(r,std::array<std::uint8_t,1>{1});}
    catch(const std::length_error&){}
    catch(const std::runtime_error&){}
    (void)box.receive();
   }
  });
  for(int i=0;i<400;++i){box.close();(void)box.restart();}
  stop=true;contender.join();
  bool denied=false;
  try{box.send(stale,std::array<std::uint8_t,1>{1});}
  catch(const std::runtime_error&){denied=true;}
  if(!denied)throw std::runtime_error("stale authority");
  box.close();auto fresh=box.restart();
  box.send(fresh,std::array<std::uint8_t,1>{7});
  auto item=box.receive();
  if(!item || *item!=std::vector<std::uint8_t>{7})
   throw std::runtime_error("fresh delivery");
  std::cout<<"PASS 6000 MPMC deliveries; 400 close/restart cycles\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL mailbox MPMC: "<<e.what()<<'\n';return 1;
 }
}
