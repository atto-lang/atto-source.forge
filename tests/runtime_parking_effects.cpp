#include "include/atto/runtime_parking_effects.hpp"
#include <algorithm>
#include <iostream>
using namespace atto;
static void check(bool ok) {if(!ok) throw std::runtime_error("parking test failed");}
static void reject(const std::vector<CallableParking>& g) {
  try { (void)infer_parking(g); } catch(const std::invalid_argument&) {return;}
  throw std::runtime_error("invalid graph admitted");
}
int main() {
  const ParkingEffects empty{}, park{false,true,false}, susp{true,false,false};
  auto rows=infer_parking({{empty,{1}}, {empty,{0,2}}, {park,{}}, {susp,{2}}, {empty,{std::nullopt}}, {empty,{4}}, {empty,{}}});
  check(rows[0]==park && rows[1]==park && rows[2]==park);
  check(rows[0].requires_resumable_frame() && !rows[0].requires_suspend_annotation() && !rows[0].safe_with_exclusive_loan());
  check(rows[3].explicit_suspend && rows[3].runtime_park && rows[5].unresolved && rows[6].safe_with_exclusive_loan());
  reject({{empty,{1}}});
  reject(std::vector<CallableParking>(1025));
  reject({{empty,std::vector<std::optional<std::size_t>>(1025)}});
  reject(std::vector<CallableParking>(17,{empty,std::vector<std::optional<std::size_t>>(1024)}));
  std::uint64_t seed=1127;
  auto next=[&seed](std::size_t m){seed=seed*6364136223846793005ULL+1ULL;return static_cast<std::size_t>((seed>>32)%m);};
  for(int trial=0;trial<256;++trial) {
    const auto n=1+next(48);
    std::vector<CallableParking> graph(n);
    for(auto& node:graph) {
      auto b=next(16);node.direct={b==1,b==2,b==3};
      auto count=next(8);
      for(std::size_t k=0;k<count;++k){auto t=next(n+3);node.calls.push_back(t<n?std::optional<std::size_t>(t):std::nullopt);}
    }
    auto actual=infer_parking(graph);
    for(std::size_t root=0;root<n;++root) {
      std::vector<bool> seen(n);
      std::vector<std::size_t> todo{root};
      ParkingEffects expected{};
      while(!todo.empty()){
        auto i=todo.back();todo.pop_back();
        if(seen[i])continue;
        seen[i]=true;
        expected.explicit_suspend|=graph[i].direct.explicit_suspend;
        expected.runtime_park|=graph[i].direct.runtime_park;
        expected.unresolved|=graph[i].direct.unresolved;
        for(auto t:graph[i].calls){if(t)todo.push_back(*t);else expected.unresolved=true;}
      }
      check(actual[root]==expected);
    }
  }
  std::vector<CallableParking> full(1024);
  for(std::size_t i=0;i<full.size();++i) full[i]={i==1023?park:empty,std::vector<std::optional<std::size_t>>(16,(i+1)%1024)};
  rows=infer_parking(full);
  check(std::all_of(rows.begin(),rows.end(),[&](auto r){return r==park;}));
  std::cout<<"PASS: Forge parking effect graph, 256 oracle trials, exact budgets and rejection fences\n";
}
