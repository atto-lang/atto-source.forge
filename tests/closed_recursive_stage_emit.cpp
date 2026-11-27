#include "atto/closed_recursive_stage.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <string_view>
using namespace atto::forge::recursive;
using atto::forge::i64;
int main(int argc,char** argv){
  assert(argc==2);
  const std::string_view id=argv[1];
  const Stage fib{{{Op::arg},{Op::literal,0,0,0},{Op::literal,0,0,1},
    {Op::literal,0,0,2},{Op::sub,0,3},{Op::self,4},{Op::sub,0,2},
    {Op::self,6},{Op::add,5,7}},0,{{0,1},{1,2},{std::nullopt,8}}};
  const Stage count{{{Op::arg},{Op::literal,0,0,0},{Op::literal,0,0,1},
    {Op::sub,0,2},{Op::self,3},{Op::add,4,2}},0,{{0,1},{std::nullopt,5}}};
  const Stage wide{{{Op::arg},{Op::literal,0,0,std::numeric_limits<i64>::max()},
    {Op::literal,0,0,1},{Op::literal,0,0,0},{Op::self,3},{Op::add,4,2}},
    0,{{0,1},{std::nullopt,5}}};
  const Stage ordered{{{Op::arg},{Op::literal,0,0,0},{Op::literal,0,0,1},
    {Op::literal,0,0,std::numeric_limits<i64>::max()},
    {Op::div,2,1},{Op::add,3,2},{Op::self,1},{Op::self,2},{Op::add,6,7}},
    0,{{0,4},{1,5},{std::nullopt,8}}};
  if(id=="invalid"){
    auto p=fib;p.nodes[5].left=5;assert(!emit(p,10));
    p=fib;p.nodes[5].right=1;assert(!emit(p,10));
    p=fib;p.nodes[0].op=static_cast<Op>(255);assert(!emit(p,10));
    p=fib;p.arms[1].key=0;assert(!emit(p,10));
    p=fib;p.arms.back().key=2;assert(!emit(p,10));
    p=fib;p.arms[0].body=p.nodes.size();assert(!emit(p,10));
    p=fib;p.subject=p.nodes.size();assert(!emit(p,10));
    p={{{Op::arg}},0,{{std::nullopt,0}}};
    for(std::size_t i=0;i<64;i++)p.nodes.push_back({Op::neg,i});
    p.arms[0].body=64;assert(!emit(p,10));
    return 0;
  }
  const Stage identity{{{Op::arg}},0,{{std::nullopt,0}}};
  const Stage* p=&fib;i64 input=10;
  if(id=="fib0")input=0;
  else if(id=="fib1")input=1;
  else if(id=="fib10")input=10;
  else if(id=="fib20")input=20;
  else if(id=="count1024"){p=&count;input=1024;}
  else if(id=="count1025"){p=&count;input=1025;}
  else if(id=="wide"){p=&wide;input=0;}
  else if(id=="overflow"){p=&wide;input=1;}
  else if(id=="ordered"){p=&ordered;input=2;}
  else if(id=="min"){p=&identity;input=std::numeric_limits<i64>::min();}
  else return 2;
  const auto code=emit(*p,input);assert(code);
  std::cout<<*code;
}
