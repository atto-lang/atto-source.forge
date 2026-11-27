#include "atto/closed_recursive_source.hpp"
#include <iostream>
#include <iterator>
int main(){
  const std::string source((std::istreambuf_iterator<char>(std::cin)),{});
  const auto plan=atto::forge::recursive::admit(source,atto::forge::scalar::tokenize(source));
  if(!plan.selected||!plan.stage)return 2;
  const auto generated=atto::forge::recursive::emit(plan.stage->body,plan.stage->input);
  if(!generated)return 2;
  std::cout<<*generated;
}
