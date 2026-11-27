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
  const Stage computed{{{Op::literal,0,0,2},{Op::literal,0,0,3},{Op::mul,0,1}},0,{{std::nullopt,2}}};
  const Stage count{{{Op::arg},{Op::literal,0,0,0},{Op::literal,0,0,1},{Op::sub,0,2},{Op::self,3},{Op::add,4,2}},0,{{0,1},{std::nullopt,5}}};
  const Stage times7{{{Op::arg},{Op::literal,0,0,7},{Op::mul,0,1}},0,{{std::nullopt,2}}};
  const Stage divzero{{{Op::literal,0,0,1},{Op::literal,0,0,0},{Op::div,0,1}},0,{{std::nullopt,2}}};
  const Stage overflow{{{Op::literal,0,0,std::numeric_limits<i64>::max()},{Op::literal,0,0,1},{Op::add,0,1}},0,{{std::nullopt,2}}};
  const Stage boundary{{{Op::literal,0,0,1024}},0,{{std::nullopt,0}}};
  const Stage too_deep{{{Op::literal,0,0,1025}},0,{{std::nullopt,0}}};
  if(id=="invalid"){
    assert(!emit_pipeline(computed,{}));
    assert(!emit_pipeline(computed,std::vector<Stage>(17,count)));
    assert(!emit_pipeline(count,{times7}));
    auto dead=computed;dead.nodes.push_back({Op::self,0});assert(!emit_pipeline(dead,{count}));
    auto bad=count;bad.arms.back().body=999;assert(!emit_pipeline(computed,{count,bad}));
    return 0;
  }
  std::optional<std::string> code;
  if(id=="chain")code=emit_pipeline(computed,{count,times7});
  else if(id=="depth-reset")code=emit_pipeline(boundary,{count,count});
  else if(id=="quota")code=emit_pipeline(too_deep,{count,times7});
  else if(id=="upstream-first")code=emit_pipeline(divzero,{overflow});
  else if(id=="stage-first")code=emit_pipeline(computed,{overflow,divzero});
  else if(id=="max-stages")code=emit_pipeline(computed,std::vector<Stage>(16,count));
  else return 2;
  assert(code);std::cout<<*code;
}
