#include "atto/reusable_slot.hpp"
#include <cassert>
#include <iostream>
using namespace atto::forge::scalar;
using K = ReusableSlot::Kind;
int main() {
  const ReusableSlot good{{40,2},{{K::bind,0},{K::read,0},{K::detach,0},{K::bind,1},{K::read,1},{K::detach,0}},{{false,2},{false,3}}};
  int n=0;
  auto check=[&](bool b){assert(b);++n;};
  auto reject=[&](auto change){auto p=good;change(p);check(!verify_reusable_slot(p));};
  check(verify_reusable_slot(good)==42);
  check(verify_reusable_slot(ReusableSlot{{},{},{{true,42}}})==42);
  check(verify_reusable_slot(ReusableSlot{{42},{{K::bind,0},{K::detach,0}},{{false,0}}})==42);
  reject([](auto& p){p.steps[1].index=1;});
  reject([](auto& p){p.steps[0].index=2;});
  reject([](auto& p){p.steps.erase(p.steps.begin());});
  reject([](auto& p){p.steps.pop_back();});
  reject([](auto& p){p.steps.push_back({K::detach,0});});
  reject([](auto& p){p.steps[2]={K::bind,1};});
  reject([](auto& p){p.steps[2].index=1;});
  reject([](auto& p){p.steps[0].kind=static_cast<K>(99);});
  reject([](auto& p){p.result[0].value=4;});
  reject([](auto& p){p.result[0].value=-1;});
  reject([](auto& p){p.result[0].subtract=true;});
  reject([](auto& p){p.result.clear();});
  reject([](auto& p){p.owners.resize(17);});
  reject([](auto& p){p.steps.resize(33);});
  reject([](auto& p){p.result.resize(33);});
  { auto p=good;p.owners={INT64_MIN,INT64_MAX};check(verify_reusable_slot(p)==-1); }
  { auto p=good;p.owners={INT64_MIN,INT64_MIN};p.result[1].subtract=true;check(verify_reusable_slot(p)==0); }
  check(verify_reusable_slot(ReusableSlot{{},{},{{true,INT64_MIN}}})==INT64_MIN);
  check(verify_reusable_slot(ReusableSlot{{},{},{{true,-1},{true,INT64_MIN,true}}})==INT64_MAX);
  reject([](auto& p){p.result={{true,INT64_MIN},{true,1,true}};});
  reject([](auto& p){p.result={{true,0},{true,INT64_MIN,true}};});
  reject([](auto& p){p.result={{true,INT64_MAX},{true,-1,true}};});
  reject([](auto& p){p.result={{true,INT64_MIN},{true,-1}};});
  reject([](auto& p){p.result={{true,INT64_MAX},{true,1}};});
  reject([](auto& p){p.result={{true,0},{true,INT64_MAX,true},{true,2,true}};});
  std::cout<<n<<" typed verifier checks passed\n";
}
