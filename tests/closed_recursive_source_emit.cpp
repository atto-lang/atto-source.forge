#include "atto/closed_recursive_source.hpp"
#include <iostream>
#include <iterator>
int main(){
  const std::string source((std::istreambuf_iterator<char>(std::cin)),{});
  const auto plan=atto::forge::recursive::admit(source);
  if(!plan.selected||!plan.stage)return 2;
  const auto generated=atto::forge::recursive::emit_pipeline(plan.stage->upstream,plan.stage->stages);
  if(!generated)return 2;
  std::cout<<*generated;
}
