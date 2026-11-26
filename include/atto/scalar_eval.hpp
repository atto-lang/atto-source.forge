#pragma once
// Restricted scalar AST evaluation with explicit arithmetic traps.
// Host-model verification only: no executable .atto guest generated here.
#include "atto/scalar_parser.hpp"
#include <optional>
#include <string_view>
#include <vector>

namespace atto::forge::scalar {
enum class EvalError { none, parse_error, invalid_ast, overflow, divide_by_zero, shift_distance, refinement_violation };
struct Evaluated {
  std::optional<i64> value;
  EvalError error = EvalError::none;
  [[nodiscard]] bool ok() const noexcept {
    return error == EvalError::none && value.has_value();
  }
};
// Host-only diagnostic model for the lazy DAG; actual guest gates compile
// emitted C++ separately. Backward-only validated edges bound recursion.
[[nodiscard]] inline Evaluated evaluate_lazy(const Parsed& p) {
  if (!validate_ast(p) || !p.trace_steps.empty()) return {{}, EvalError::invalid_ast};
  std::vector<std::optional<i64>> cache(p.nodes.size());
  auto run=[&](auto&& self,std::size_t i)->Evaluated {
    if(cache[i])return {cache[i],EvalError::none};
    const auto& n=p.nodes[i];
    std::optional<i64> value;
    if(n.op==Op::literal)value=n.value;
    else{
      auto a=self(self,n.left);if(!a.ok())return a;
      const auto x=*a.value;
      if(n.op==Op::choose){auto b=self(self,x!=0?n.right:static_cast<std::size_t>(n.value));if(!b.ok())return b;value=b.value;}
      else if(n.op==Op::copy)value=x;
      else if(n.op==Op::neg)value=checked_neg(x);
      else if(n.op==Op::refine){
        if(x<n.value||x>n.upper)return {{},EvalError::refinement_violation};
        value=x;
      }else{
        if(n.op==Op::bnot)return {{},EvalError::invalid_ast};
        auto b=self(self,n.right);if(!b.ok())return b;
        const auto y=*b.value;
        switch(n.op){
          case Op::sequence:value=y;break;
          case Op::eq:value=x==y;break;case Op::ne:value=x!=y;break;
          case Op::lt:value=x<y;break;case Op::le:value=x<=y;break;
          case Op::gt:value=x>y;break;case Op::ge:value=x>=y;break;
          case Op::add:value=checked_add(x,y);break;
          case Op::sub:value=checked_sub(x,y);break;
          case Op::mul:value=checked_mul(x,y);break;
          case Op::div:
            if(y==0)return {{},EvalError::divide_by_zero};
            value=checked_div(x,y);break;
          default:return {{},EvalError::invalid_ast};
        }
      }
    }
    if(!value)return {{},EvalError::overflow};
    cache[i]=value;return {value,EvalError::none};
  };
  return run(run,p.root);
}
[[nodiscard]] inline Evaluated evaluate(const Parsed& p) {
  if (!validate_ast(p)) return {{}, EvalError::invalid_ast};
  for(const auto& n:p.nodes)if(n.op==Op::choose)return evaluate_lazy(p);
  std::vector<i64> values;
  values.reserve(p.nodes.size());
  for (std::size_t i = 0; i < p.nodes.size(); ++i) {
    const Node& node = p.nodes[i];
    if (node.op == Op::literal) {
      values.push_back(node.value);
      continue;
    }
    if(node.op==Op::copy||node.op==Op::sequence){
      const auto value=values[node.op==Op::copy?node.left:node.right];
      values.push_back(value);continue;
    }
    if (node.op==Op::refine) {
      const auto value=values[node.left];
      if (value<node.value || value>node.upper) return {{},EvalError::refinement_violation};
      values.push_back(value);continue;
    }
    if (node.left >= i || (node.op != Op::neg && node.op != Op::bnot && node.right >= i))
      return {{}, EvalError::invalid_ast};
    const i64 lhs = values[node.left];
    std::optional<i64> result;
    switch (node.op) {
      case Op::bnot:
        result = bit_not(lhs);
        break;
      case Op::neg:
        result = checked_neg(lhs);
        break;
      case Op::add:
        result = checked_add(lhs, values[node.right]);
        break;
      case Op::sub:
        result = checked_sub(lhs, values[node.right]);
        break;
      case Op::mul:
        result = checked_mul(lhs, values[node.right]);
        break;
      case Op::div:
        if (values[node.right] == 0) return {{}, EvalError::divide_by_zero};
        result = checked_div(lhs, values[node.right]);
        break;
      case Op::shl:
        if (values[node.right] < 0 || values[node.right] > 63)
          return {{}, EvalError::shift_distance};
        result = shl_i64(lhs, values[node.right]);
        break;
      case Op::lshr:
        if (values[node.right] < 0 || values[node.right] > 63)
          return {{}, EvalError::shift_distance};
        result = lshr_i64(lhs, values[node.right]);
        break;
      case Op::ashr:
        if (values[node.right] < 0 || values[node.right] > 63)
          return {{}, EvalError::shift_distance};
        result = ashr_i64(lhs, values[node.right]);
        break;
      case Op::band:
        result = bit_and(lhs, values[node.right]);
        break;
      case Op::bor:
        result = bit_or(lhs, values[node.right]);
        break;
      case Op::bxor:
        result = bit_xor(lhs, values[node.right]);
        break;
      case Op::rem:
        if (values[node.right] == 0) return {{}, EvalError::divide_by_zero};
        result = checked_rem(lhs, values[node.right]);
        break;
      default:
        return {{}, EvalError::invalid_ast};
    }
    if (!result) return {{}, EvalError::overflow};
    values.push_back(*result);
  }
  return {values[p.root], EvalError::none};
}
[[nodiscard]] inline Evaluated evaluate_source(std::string_view src) {
  const auto p = parse_source(src);
  if (!p.ok()) return {{}, EvalError::parse_error};
  return evaluate(p);
}
}  // namespace atto::forge::scalar
