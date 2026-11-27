#pragma once
// Restricted native Atto scalar parser: one main() with a scalar return.
#include "atto/scalar_lexer.hpp"
#include "atto/reusable_slot.hpp"
#include "atto/point_placement.hpp"
#include "atto/decimal_i64.hpp"
#include "atto/exact_quantity_scale.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atto::forge::scalar {
enum class Op { literal, add, sub, mul, div, rem, neg, refine, narrow, copy, sequence, band, bor, bxor, bnot, shl, lshr, ashr, eq, ne, lt, le, gt, ge, choose, loop_state, loop_value, loop_group, loop_read };
struct LoopExit { std::size_t position=0, condition=0; bool breaking=false; std::vector<std::size_t> values{}; };
struct Node {
  Op op;
  std::size_t left = 0, right = 0;
  i64 value = 0;
  i64 upper = 0;
  std::vector<std::size_t> states{},effects{},updates{};
  std::vector<LoopExit> exits{};
};
enum class ParseError {
  none, lexical, ownership_bang_unsupported, suspension_unsupported, pointer_profile, syntax, literal_out_of_range, depth_limit, node_limit, telemetry, verify_unproven, verify_unsupported, unit_name, unit_duplicate, unit_dimension, unit_scale, unit_limit, unit_erasure, unit_attachment, unit_inexact, unit_range, refinement_empty, refinement_binding, refinement_precondition, refinement_return, refinement_duplicate, refinement_postcondition, refinement_contract, refinement_export, refinement_body, refinement_call, refinement_cast, i32_range, new_required, lock_unsupported, task_unsupported, task_return_type, pair_key_type, pair_scope, pair_escape, pair_projection, pair_key_unsupported, missing_initializer, entry_scope, entry_escape, entry_projection, source_bidi, bool_operand, duplicate_local, unbound_local, spawn_options_unsupported, stdout_profile_unsupported
};
// Stable sanitized diagnostics; never echo user source bytes.
[[nodiscard]] constexpr std::string_view parse_error_name(ParseError error) noexcept {
  switch (error) {
    case ParseError::none: return "NONE";
    case ParseError::lexical: return "E_PARSE_LEXICAL";
    case ParseError::source_bidi: return "E_SOURCE_BIDI";
    case ParseError::bool_operand: return "E_BOOL_OPERAND";
    case ParseError::duplicate_local: return "E_DUPLICATE_LOCAL";
    case ParseError::unbound_local: return "E_UNBOUND_LOCAL";
    case ParseError::ownership_bang_unsupported: return "E_OWNERSHIP_BANG_UNSUPPORTED";
    case ParseError::suspension_unsupported: return "E_SUSPENDS_UNSUPPORTED";
    case ParseError::spawn_options_unsupported: return "E_SPAWN_OPTIONS_UNSUPPORTED";
    case ParseError::stdout_profile_unsupported: return "E_STDOUT_PROFILE_UNSUPPORTED";
    case ParseError::pointer_profile: return "E_POINTER_PROFILE";
    case ParseError::syntax: return "E_PARSE_SYNTAX";
    case ParseError::literal_out_of_range: return "E_PARSE_RANGE";
    case ParseError::depth_limit: return "E_PARSE_DEPTH";
    case ParseError::node_limit: return "E_PARSE_NODES";
    case ParseError::telemetry: return "E_TELEMETRY_SCOPE";
    case ParseError::new_required: return "E_NEW_REQUIRED";
    case ParseError::lock_unsupported: return "E_LOCK_UNSUPPORTED";
    case ParseError::task_unsupported: return "E_TASK_UNSUPPORTED";
    case ParseError::task_return_type: return "E_TASK_RETURN_TYPE";
    case ParseError::pair_key_type: return "E_PAIR_KEY_TYPE";
    case ParseError::pair_scope: return "E_PAIR_SCOPE_UNSUPPORTED";
    case ParseError::pair_escape: return "E_PAIR_ESCAPE_UNSUPPORTED";
    case ParseError::pair_projection: return "E_PAIR_PROJECTION_UNSUPPORTED";
    case ParseError::pair_key_unsupported: return "E_PAIR_KEY_UNSUPPORTED";
    case ParseError::entry_scope: return "E_ENTRY_SCOPE_UNSUPPORTED";
    case ParseError::entry_escape: return "E_ENTRY_ESCAPE_UNSUPPORTED";
    case ParseError::entry_projection: return "E_ENTRY_PROJECTION_UNSUPPORTED";
    case ParseError::missing_initializer: return "E_MISSING_INITIALIZER";
    case ParseError::verify_unproven: return "E_VERIFY_UNPROVEN";
    case ParseError::verify_unsupported: return "E_VERIFY_UNSUPPORTED";
    case ParseError::unit_name: return "E_UNIT_NAME";
    case ParseError::unit_duplicate: return "E_UNIT_DUPLICATE";
    case ParseError::unit_dimension: return "E_UNIT_DIMENSION";
    case ParseError::unit_scale: return "E_UNIT_SCALE";
    case ParseError::unit_limit: return "E_UNIT_LIMIT";
    case ParseError::unit_erasure: return "E_UNIT_ERASURE";
    case ParseError::unit_attachment: return "E_UNIT_ATTACHMENT";
    case ParseError::unit_inexact: return "E_UNIT_INEXACT";
    case ParseError::unit_range: return "E_UNIT_RANGE";
    case ParseError::refinement_empty: return "E_REFINEMENT_EMPTY";
    case ParseError::refinement_binding: return "E_REFINEMENT_BINDING";
    case ParseError::refinement_precondition: return "E_REFINEMENT_PRECONDITION";
    case ParseError::refinement_return: return "E_REFINEMENT_RETURN";
    case ParseError::refinement_duplicate: return "E_REFINEMENT_DUPLICATE";
    case ParseError::refinement_postcondition: return "E_REFINEMENT_POSTCONDITION";
    case ParseError::refinement_contract: return "E_REFINEMENT_CONTRACT";
    case ParseError::refinement_export: return "E_REFINEMENT_EXPORT";
    case ParseError::refinement_body: return "E_REFINEMENT_BODY";
    case ParseError::refinement_call: return "E_REFINEMENT_CALL";
    case ParseError::refinement_cast: return "E_REFINEMENT_CAST";
    case ParseError::i32_range: return "E_I32_RANGE";
  }
  return "E_PARSE_UNKNOWN";
}

// BorrowedLocal is NOT an owning Pointer.alloc<T> region or a raw Atto
// pointer. The canonical parser verifies the exact lexical sequence and
// erases all source names before emission. Only a single initialized i64
// place with a compiler-authenticated readonly handle is admitted.
struct BorrowedLocal {
  std::string referent;
  std::string handle;
  std::string copied_scalar;
  i64 initial = 0;
};
struct Parsed {
  std::optional<BorrowedLocal> local;
  std::vector<Node> nodes;
  std::size_t root = 0;
  ParseError error = ParseError::none;
  std::size_t error_offset = 0;
  // Named entries open a scope. Empty entries close the innermost scope.
  // Remaining scopes close with the terminal scalar result or checked error.
  std::vector<std::optional<std::string>> trace_steps;
  std::optional<ReusableSlot> reusable_slot;
  std::optional<PointPlacement> point_placement;
  [[nodiscard]] bool ok() const noexcept {
    return error == ParseError::none && !nodes.empty() && root < nodes.size();
  }
};
inline constexpr std::size_t max_nodes = 256;
inline constexpr std::size_t max_strict_nodes = 128;
inline constexpr std::size_t max_ordinary_bindings = 64;
inline constexpr int max_parse_depth = 24;
inline constexpr std::size_t max_trace_depth = 16;

[[nodiscard]] inline bool valid_operation(std::string_view operation) {
  if (operation.empty() || operation.size() > 256) return false;
  for (unsigned char byte : operation) {
    if (byte < 32 || byte > 126 || byte == '"' || byte == '\\') return false;
  }
  return true;
}

// Verify all untrusted or externally constructed ASTs before evaluation or
// code emission. A valid postorder AST has the root as its final node,
// backward-only edges, no detached nodes, and recognized operation tags.
[[nodiscard]] inline bool validate_ast(const Parsed& p) {
  if (!p.ok() || p.nodes.size() > max_nodes ||
      p.root != p.nodes.size() - 1) return false;
  if (p.point_placement) {
    const auto value=verify_point_placement(*p.point_placement);
    if(!value||p.local||p.reusable_slot||!p.trace_steps.empty()||p.nodes.size()!=1||
       p.root!=0||p.nodes[0].op!=Op::literal||p.nodes[0].value!=*value||
       p.nodes[0].left||p.nodes[0].right||p.nodes[0].upper||
       !p.nodes[0].states.empty()||!p.nodes[0].effects.empty()||
       !p.nodes[0].updates.empty()||!p.nodes[0].exits.empty())return false;
  }
  if (p.reusable_slot) {
    const auto value = verify_reusable_slot(*p.reusable_slot);
    if (!value || p.local || !p.trace_steps.empty() || p.nodes.size() != 1 ||
        p.root != 0 || p.nodes[0].op != Op::literal || p.nodes[0].value != *value ||
        p.nodes[0].left || p.nodes[0].right || p.nodes[0].upper ||
        !p.nodes[0].states.empty() || !p.nodes[0].effects.empty() ||
        !p.nodes[0].updates.empty() || !p.nodes[0].exits.empty()) return false;
  }
  if (p.local) {
    // A forged or mutated typed plan must not be allowed to change its
    // value, launder a raw address, or become an owning pointer. There is
    // exactly one canonical postorder i64 read-copy result.
    const auto& l = *p.local;
    const auto safe_name = [](const std::string& n) {
      if (n.empty() || n.size() > 96) return false;
      auto start = [](char ch) {
        return (ch >= 'a' && ch <= 'z') ||
               (ch >= 'A' && ch <= 'Z') || ch == '_';
      };
      if (!start(n[0])) return false;
      for (char ch : n)
        if (!start(ch) && !(ch >= '0' && ch <= '9')) return false;
      return true;
    };
    const auto reserved = [](const std::string& n) {
      return n == "main" || n == "int" || n == "Pointer" ||
             n == "let" || n == "val" || n == "rt" ||
             n == "ref" || n == "deref" || n == "unref" ||
             n == "return" || n == "fnc" || n == "pub";
    };
    if (!p.trace_steps.empty() || !safe_name(l.referent) || !safe_name(l.handle) ||
        !safe_name(l.copied_scalar) ||
        reserved(l.referent) || reserved(l.handle) ||
        reserved(l.copied_scalar) ||
        l.referent == l.handle || l.referent == l.copied_scalar ||
        l.handle == l.copied_scalar ||
        p.nodes.size() != 1 || p.root != 0 ||
        p.nodes[0].op != Op::literal || p.nodes[0].value != l.initial ||
        p.nodes[0].left != 0 || p.nodes[0].right != 0 || p.nodes[0].upper != 0 ||
        !p.nodes[0].states.empty() || !p.nodes[0].effects.empty() || !p.nodes[0].updates.empty())
      return false;
  }
  if (p.trace_steps.size() > max_tokens) return false;
  std::size_t trace_depth = 0;
  for (const auto& operation : p.trace_steps) {
    if (operation) {
      if (!valid_operation(*operation) || ++trace_depth > max_trace_depth) return false;
    } else {
      if (trace_depth == 0) return false;
      --trace_depth;
    }
  }
  // Each loop owns a private contiguous cache region. Nested regions must
  // be strictly contained; only the completed group and projections escape.
  std::vector<std::pair<std::size_t,std::size_t>> regions;
  std::vector<unsigned> owners(p.nodes.size(),0);
  for(std::size_t i=0;i<p.nodes.size();++i){
    const auto& n=p.nodes[i];
    if(n.op!=Op::loop_group&&(!n.states.empty()||!n.effects.empty()||!n.updates.empty()||!n.exits.empty()))return false;
    if(n.op==Op::loop_value){
      if(n.left>=i||p.nodes[n.left].op!=Op::loop_state||n.right<=n.left||n.right>=i||n.value<0||
         static_cast<std::uint64_t>(n.value)<=n.left||static_cast<std::uint64_t>(n.value)>=i)return false;
      if(++owners[n.left]!=1)return false;
      regions.emplace_back(n.left,i);
    }
    if(n.op==Op::loop_group){
      if(n.states.empty()||n.states.size()>32||n.states.size()!=n.updates.size()||
         n.left!=n.states.front()||n.left>=i||n.right<n.left+n.states.size()||n.right>=i||n.value!=0)return false;
      for(std::size_t j=0;j<n.states.size();++j){
        auto k=n.states[j];
        if(k!=n.left+j||k>=i||p.nodes[k].op!=Op::loop_state||p.nodes[k].left>=n.left||++owners[k]!=1)return false;
      }
      // Effects must be explicit, unique, ordered lexical evaluations.
      // A transfer snapshot may only reference its own initial slot or a
      // version evaluated in the already-executed effect prefix.
      std::size_t prior_effect=n.right;
      for(auto k:n.effects){
        if(k<=prior_effect||k>=i)return false;
        const auto op=p.nodes[k].op;
        if(op!=Op::copy&&op!=Op::loop_group&&op!=Op::loop_read)return false;
        prior_effect=k;
      }
      const auto snapshot_ok=[&](std::size_t slot,std::size_t value,std::size_t prefix){
        if(value==n.states[slot])return true;
        if(value>=i||prefix>n.effects.size())return false;
        const auto op=p.nodes[value].op;
        if(op!=Op::copy&&op!=Op::loop_read)return false;
        return std::binary_search(n.effects.begin(),n.effects.begin()+static_cast<std::ptrdiff_t>(prefix),value);
      };
      for(std::size_t j=0;j<n.updates.size();++j)
        if(!snapshot_ok(j,n.updates[j],n.effects.size()))return false;
      if(n.exits.size()>128)return false;
      std::size_t prior_position=0,prior_condition=n.right;
      for(const auto& x:n.exits){
        if(x.position<prior_position||x.position>n.effects.size()||x.condition<=prior_condition||
           x.condition<n.left+n.states.size()||x.condition>=i||x.values.size()!=n.states.size())return false;
        if(x.position&&n.effects[x.position-1]>=x.condition)return false;
        if(x.position<n.effects.size()&&n.effects[x.position]<=x.condition)return false;
        for(std::size_t j=0;j<x.values.size();++j)
          if(x.values[j]>=x.condition||!snapshot_ok(j,x.values[j],x.position))return false;
        prior_position=x.position;prior_condition=x.condition;
      }
      regions.emplace_back(n.left,i);
    }
    if(n.op==Op::loop_read){
      if(n.left>=i||n.right!=0||n.value<0||p.nodes[n.left].op!=Op::loop_group||
         static_cast<std::uint64_t>(n.value)>=p.nodes[n.left].states.size())return false;
    }
  }
  if(!regions.empty()&&!p.trace_steps.empty())return false;
  for(std::size_t j=0;j<regions.size();++j)for(std::size_t k=j+1;k<regions.size();++k){
    auto [a,b]=regions[j];auto [c,d]=regions[k];
    if(a<=d&&c<=b&&!((a<c&&d<b)||(c<a&&b<d)))return false;
  }
  for(std::size_t i=0;i<p.nodes.size();++i){
    const auto& n=p.nodes[i];
    if(n.op==Op::loop_state&&owners[i]!=1)return false;
    auto edge=[&](std::size_t k){for(auto [a,b]:regions)if(k>=a&&k<b&&(i<=a||i>b))return false;return true;};
    if(n.op==Op::literal)continue;
    if(!edge(n.left))return false;
    if(n.op==Op::refine||n.op==Op::narrow||n.op==Op::neg||n.op==Op::bnot||n.op==Op::copy||n.op==Op::loop_state||n.op==Op::loop_read)continue;
    if(!edge(n.right))return false;
    if(n.op==Op::loop_group){
      for(auto k:n.states)if(!edge(k))return false;
      for(auto k:n.effects)if(!edge(k))return false;
      for(auto k:n.updates)if(!edge(k))return false;
      for(const auto& x:n.exits){if(!edge(x.condition))return false;for(auto k:x.values)if(!edge(k))return false;}
    }
    if((n.op==Op::choose||n.op==Op::loop_value)&&(n.value<0||!edge(static_cast<std::size_t>(n.value))))return false;
  }
  std::vector<unsigned char> reachable(p.nodes.size(), 0);
  std::vector<std::size_t> pending{p.root};
  while (!pending.empty()) {
    const auto index = pending.back();
    pending.pop_back();
    if (index >= p.nodes.size()) return false;
    if (reachable[index]) continue;
    reachable[index] = 1;
    const auto& node = p.nodes[index];
    if (node.op != Op::refine && node.upper != 0) return false;
    switch (node.op) {
      case Op::literal:
        if (node.left != 0 || node.right != 0) return false;
        break;
      case Op::refine:
        if (node.value > node.upper || node.left >= index || node.right != 0) return false;
        pending.push_back(node.left);
        break;
      case Op::loop_state:
      case Op::narrow:
      case Op::copy:
      case Op::neg:
      case Op::bnot:
        if (node.left >= index || node.right != 0) return false;
        pending.push_back(node.left);
        break;
      case Op::loop_group:
        pending.push_back(node.right);
        for(auto k:node.states)pending.push_back(k);
        for(auto k:node.effects)pending.push_back(k);
        for(auto k:node.updates)pending.push_back(k);
        for(const auto& x:node.exits){pending.push_back(x.condition);for(auto k:x.values)pending.push_back(k);}
        break;
      case Op::loop_read:
        pending.push_back(node.left);break;
      case Op::loop_value:
      case Op::choose:
        if (node.left >= index || node.right >= index || node.value < 0 ||
            static_cast<std::uint64_t>(node.value) >= index) return false;
        pending.push_back(node.left); pending.push_back(node.right);
        pending.push_back(static_cast<std::size_t>(node.value));
        break;
      case Op::eq: case Op::ne: case Op::lt: case Op::le: case Op::gt: case Op::ge:
        if (node.left >= index || node.right >= index) return false;
        pending.push_back(node.left); pending.push_back(node.right); break;
      case Op::sequence:
        if (node.left >= node.right || node.right >= index) return false;
        pending.push_back(node.left);pending.push_back(node.right);
        break;
      case Op::add:
      case Op::sub:
      case Op::mul:
      case Op::div:
      case Op::rem:
      case Op::band:
      case Op::bor:
      case Op::bxor:
      case Op::shl:
      case Op::lshr:
      case Op::ashr:
        if (node.left >= index || node.right >= index) return false;
        pending.push_back(node.left);
        pending.push_back(node.right);
        break;
      default: return false;
    }
  }
  for (auto seen : reachable) if (!seen) return false;
  return true;
}

// Numeric separators belong to source syntax, not the host decimal API.
// Require a digit on both sides; retain the bounded canonical conversion.
[[nodiscard]] inline std::optional<i64> parse_source_decimal(std::string_view text) {
  if(text.empty()||text.size()>64)return std::nullopt;
  std::string digits;digits.reserve(text.size());
  auto digit=[](char c){return c>='0'&&c<='9';};
  for(std::size_t i=0;i<text.size();++i){
    if(text[i]=='_'){
      if(i==0||i+1==text.size()||!digit(text[i-1])||!digit(text[i+1]))return std::nullopt;
    }else digits.push_back(text[i]);
  }
  return parse_decimal_i64(digits);
}
class Parser {
  std::string_view source_;
  std::vector<Token> tokens_;
  std::size_t pos_ = 0;
  Parsed out_;
  bool strict_ = false;
  bool guarded_ = false;
  // File-local bounded rational dimensions; erased only after scalar admission.
  using Dim = std::array<int, 8>;
  using Scale = ExactScale;
  std::vector<Dim> dims_;
  std::vector<Scale> scales_;
  enum class Width {literal,i32,i64};
  static constexpr i64 i32_lo=std::numeric_limits<std::int32_t>::min(), i32_hi=std::numeric_limits<std::int32_t>::max();
  struct Range {
    i64 lo=std::numeric_limits<i64>::min(),hi=std::numeric_limits<i64>::max();
    Width width=Width::i64;
    unsigned holes=0;
    [[nodiscard]] Range intersect(i64 l,i64 h,Width w)const{
      const i64 a=lo>l?lo:l,b=hi<h?hi:h;
      return a>b?Range{l,h,w}:Range{a,b,w,holes};
    }
    [[nodiscard]] bool within(i64 l,i64 h) const {return lo>=l&&hi<=h;}
    static unsigned bit(i64 n){return n==0?1u:n==-1?2u:0u;}
    bool has(i64 n) const{return lo<=n&&n<=hi&&!(holes&bit(n));}
    Range restrict(Op op,i64 n) const{
      auto r=*this;
      switch(op){
        case Op::eq:r.lo=std::max(r.lo,n);r.hi=std::min(r.hi,n);break;
        case Op::ne:
          if(!r.has(n))return *this;
          if(r.lo==r.hi){
            // Contradictory singleton on a split path: retain only the
            // representable guard domain, never certify arbitrary dead code.
            if(!bit(n))return *this;
            return width==Width::i32?Range{INT32_MIN,INT32_MAX,width,bit(n)}:
                Range{std::numeric_limits<i64>::min(),std::numeric_limits<i64>::max(),width,bit(n)};
          }
          if(r.lo==n)++r.lo;else if(r.hi==n)--r.hi;else r.holes|=bit(n);break;
        case Op::lt:if(n==std::numeric_limits<i64>::min())return *this;r.hi=std::min(r.hi,n-1);break;
        case Op::le:r.hi=std::min(r.hi,n);break;
        case Op::gt:if(n==std::numeric_limits<i64>::max())return *this;r.lo=std::max(r.lo,n+1);break;
        case Op::ge:r.lo=std::max(r.lo,n);break;
        default:return *this;
      }
      while(r.lo<=r.hi&&(r.holes&bit(r.lo)))++r.lo;
      while(r.lo<=r.hi&&(r.holes&bit(r.hi)))--r.hi;
      return r.lo<=r.hi?r:*this;
    }
    Range join(Range r) const{
      return {std::min(lo,r.lo),std::max(hi,r.hi),width==r.width?width:Width::i64,
        (!has(0)&&!r.has(0)?1u:0u)|(!has(-1)&&!r.has(-1)?2u:0u)};
    }
    [[nodiscard]] static Range exact(i64 n){return {n,n,Width::literal};}
    [[nodiscard]] Range neg() const {
      const auto a=checked_neg(hi),b=checked_neg(lo);
      return a&&b?Range{*a,*b}:Range{};
    }
    [[nodiscard]] bool proves(Range r, Op op) const {
      if(op==Op::add)return checked_add(lo,r.lo).has_value()&&checked_add(hi,r.hi).has_value();
      if(op==Op::sub)return checked_sub(lo,r.hi).has_value()&&checked_sub(hi,r.lo).has_value();
      if(op==Op::mul){for(auto a:{lo,hi})for(auto b:{r.lo,r.hi})if(!checked_mul(a,b))return false;return true;}
      if(op==Op::div)return !r.has(0)&&!(has(std::numeric_limits<i64>::min())&&r.has(-1));
      return false;
    }
    [[nodiscard]] Range binary(Range r,Op op) const {
      if(op==Op::add||op==Op::sub){
        const auto a=op==Op::add?checked_add(lo,r.lo):checked_sub(lo,r.hi);
        const auto b=op==Op::add?checked_add(hi,r.hi):checked_sub(hi,r.lo);
        // Normal successors exclude trapping arithmetic. Saturate the
        // interval endpoints; strict admission separately proves no trap.
        const auto min=std::numeric_limits<i64>::min(),max=std::numeric_limits<i64>::max();
        const auto lower=a.value_or((op==Op::add?r.lo<0:r.hi>0)?min:max);
        const auto upper=b.value_or((op==Op::add?r.hi<0:r.lo>0)?min:max);
        return lower<=upper?Range{lower,upper}:Range{};
      }
      if(op==Op::mul||(op==Op::div&&(r.lo>0||r.hi<0))){
        Range result{std::numeric_limits<i64>::max(),std::numeric_limits<i64>::min()};
        for(auto a:{lo,hi})for(auto b:{r.lo,r.hi}){
          const auto n=op==Op::mul?checked_mul(a,b):checked_div(a,b);
          if(!n)return {};
          if(*n<result.lo)result.lo=*n;
          if(*n>result.hi)result.hi=*n;
        }
        return result;
      }
      return {};
    }
  };
  std::vector<Range> ranges_;
  std::vector<std::pair<std::string_view,std::size_t>> bindings_;
  // Boolean slots are admitted only by the ordinary typed predicate path.
  // They never flow into the scalar numeric expression parser.
  std::vector<std::size_t> boolean_slots_;
  bool boolean_slot(std::size_t slot) const {
    return std::find(boolean_slots_.begin(),boolean_slots_.end(),slot)!=boolean_slots_.end();
  }
  std::optional<std::size_t> boolean_binding(std::string_view spelling) const {
    for(const auto& b:bindings_)if(b.first==spelling&&boolean_slot(b.second))return b.second;
    return std::nullopt;
  }
  std::vector<std::string_view> dimensions_;
  struct Unit { std::string_view name; std::size_t dim; Scale scale; };
  std::vector<Unit> units_;
  struct Refinement { std::string_view name; Dim dim; Scale scale; i64 lo, hi; Width width=Width::i64; };
  std::vector<Refinement> refinements_;
  struct Helper {
    std::string_view name;
    std::vector<Refinement> params;
    Refinement result;
    std::vector<Node> nodes;
    std::vector<Dim> dims;
    std::vector<Scale> scales;
    std::vector<Range> ranges;
  };
  std::vector<Helper> helpers_;
  std::vector<std::pair<std::string_view,std::vector<std::size_t>>> strict_arrays_;
  static constexpr int exponent_limit = 32;
  [[nodiscard]] bool dimensionless(std::size_t n) const { return dims_[n] == Dim{} && scales_[n] == Scale{}; }
  [[nodiscard]] bool known_name(std::string_view n) const {
    for (auto d : dimensions_) if (d == n) return true;
    for (auto u : units_) if (u.name == n) return true;
    for (const auto& r : refinements_) if (r.name==n) return true;
    return false;
  }
  [[nodiscard]] bool declaration_name(std::string_view n) const {
    // This bounded profile uses conventional upper-case nominal names.
    return !n.empty() && n[0] >= 'A' && n[0] <= 'Z' && !known_name(n);
  }
  [[nodiscard]] std::optional<i64> factor() {
    if (current().kind!=Kind::integer) {fail(ParseError::unit_scale);return std::nullopt;}
    auto value=parse_source_decimal(text());
    if (!value || *value<=0) {fail(ParseError::unit_scale);return std::nullopt;}
    ++pos_; return value;
  }
  [[nodiscard]] bool declarations() {
    while (name("define")) {
      const bool dimension = name("dimension");
      if ((!dimension && !name("unit")) || current().kind != Kind::name) return false;
      if (known_name(text())) {fail(ParseError::unit_duplicate);return false;}
      if(!declaration_name(text())) {fail(ParseError::unit_name);return false;}
      const auto n = text(); ++pos_;
      if (dimension) {
        if (dimensions_.size() == Dim{}.size()) {fail(ParseError::unit_limit);return false;}
        dimensions_.push_back(n);
      } else {
        if (units_.size() == 32 || !take(Kind::colon) || current().kind != Kind::name) return false;
        std::size_t d = 0;
        while (d < dimensions_.size() && dimensions_[d] != text()) ++d;
        if (d == dimensions_.size()) {fail(ParseError::unit_name);return false;}
        ++pos_;
        Scale scale{};
        if (take(Kind::equals)) {
          Scale f{};
          const bool prefix=current().kind==Kind::integer;
          if (prefix) {
            const auto k=factor(); if (!k || !take(Kind::star)) return false;
            f.n=*k;
          }
          if (current().kind != Kind::name) return false;
          const Unit* base=nullptr;
          for (const auto& u : units_) if (u.name==text()) base=&u;
          if (!base) {fail(ParseError::unit_name);return false;}
          if(base->dim!=d){fail(ParseError::unit_dimension);return false;}
          ++pos_;
          if (!prefix && (current().kind==Kind::star || current().kind==Kind::slash)) {
            const bool divide=take(Kind::slash);
            if (!divide) (void)take(Kind::star);
            const auto k=factor(); if (!k) return false;
            if (divide) f.d=*k; else f.n=*k;
          }
          const auto normalized=base->scale.mul(f);
          if (!normalized) {fail(ParseError::unit_limit);return false;}
          scale=*normalized;
        } else {
          for (auto u : units_) if (u.dim == d) {fail(ParseError::unit_duplicate);return false;}
        }
        units_.push_back(Unit{n,d,scale});
      }
      if (!take(Kind::semicolon)) return false;
    }
    return true;
  }
  [[nodiscard]] std::optional<i64> signed_bound() {
    const bool neg=take(Kind::minus);
    if (current().kind!=Kind::integer) return std::nullopt;
    const std::string digits=(neg?"-":"")+std::string(text());
    const auto result=parse_source_decimal(digits);
    if (result) ++pos_;
    return result;
  }
  [[nodiscard]] bool refinement_declarations() {
    while (name("type")) {
      if(current().kind==Kind::name&&(text()=="int"||text()=="i64"||text()=="i32")){fail(ParseError::refinement_duplicate);return false;}
      if (refinements_.size()==32 || current().kind!=Kind::name || !declaration_name(text())) return false;
      const auto n=text();++pos_;
      if(!take(Kind::equals))return false;
      const auto width=name("i32")?Width::i32:Width::i64;
      if(width==Width::i64&&!name("int")&&!name("i64"))return false;
      if(!name("where"))return false;
      i64 lo=width==Width::i32?i32_lo:std::numeric_limits<i64>::min(),hi=width==Width::i32?i32_hi:std::numeric_limits<i64>::max();
      std::size_t count=0;
      bool has_units=false;
      for (;;) {
        if(name("units")){has_units=true;break;}
        if (++count>16 || !name("self")) return false;
        if(name("eq")){const auto n=signed_bound();if(!n)return false;lo=lo>*n?lo:*n;hi=hi<*n?hi:*n;if(lo>hi){fail(ParseError::refinement_empty);return false;}if(!name("and"))break;continue;}
        const bool lower=take(Kind::gt);
        if (!lower && !take(Kind::lt)) return false;
        const bool inclusive=current().kind==Kind::equals && tokens_[pos_-1].offset+tokens_[pos_-1].length==current().offset;
        if (inclusive) ++pos_;
        const auto bound=signed_bound();
        if (!bound) return false;
        if (lower) {
          if (!inclusive && *bound==std::numeric_limits<i64>::max()) {fail(ParseError::refinement_empty);return false;}
          const i64 b=inclusive?*bound:*bound+1;
          if (b>lo) lo=b;
        } else {
          if (!inclusive && *bound==std::numeric_limits<i64>::min()) {fail(ParseError::refinement_empty);return false;}
          const i64 b=inclusive?*bound:*bound-1;
          if (b<hi) hi=b;
        }
        if (lo>hi) {fail(ParseError::refinement_empty);return false;}
        if (!name("and")) break;
        if (name("units")) {has_units=true;break;}
      }
      Dim dim{};Scale scale{};
      if (has_units) {
        if (!name("in") || current().kind!=Kind::name) return false;
        const Unit* unit=nullptr;
        for (const auto& u:units_) if (u.name==text()) unit=&u;
        if (!unit) return false;
        dim[unit->dim]=1;scale=unit->scale;++pos_;
      }
      if (!take(Kind::semicolon)) return false;
      refinements_.push_back(Refinement{n,dim,scale,lo,hi,width});
    }
    return true;
  }
  [[nodiscard]] const Refinement* refinement_target() const {
    if (current().kind!=Kind::name) return nullptr;
    for (const auto& r:refinements_) if (r.name==text()) return &r;
    return nullptr;
  }
  [[nodiscard]] std::optional<std::size_t> checked_refinement(std::size_t n,const Refinement& r) {
    if(dims_[n]!=r.dim){fail(ParseError::unit_dimension);return std::nullopt;}
    if(scales_[n]!=r.scale){fail(ParseError::unit_scale);return std::nullopt;}
    if(strict_&&!ranges_[n].within(r.lo,r.hi)){fail(ParseError::verify_unproven);return std::nullopt;}
    auto result=append(Node{Op::refine,n,0,r.lo,r.hi});
    if(result)ranges_[*result]=ranges_[n].intersect(r.lo,r.hi,r.width);
    return result;
  }
  [[nodiscard]] bool attach(std::size_t n) {
    if (!dimensionless(n)) {fail(ParseError::unit_attachment);return false;}
    if(current().kind!=Kind::name){fail(ParseError::unit_name);return false;}
    for (const auto& u : units_) if (u.name == text()) {
      dims_[n][u.dim] = 1; scales_[n]=u.scale; ++pos_; return true;
    }
    fail(ParseError::unit_name); return false;
  }
  [[nodiscard]] bool scalar_cast(std::size_t n) {
    if (dimensionless(n)) return true;
    fail(ParseError::unit_erasure); return false;
  }
  [[nodiscard]] const Token& current() const { return tokens_[pos_]; }
  [[nodiscard]] std::string_view text() const {
    const auto& t = current();
    return source_.substr(t.offset, t.length);
  }
  void fail(ParseError error) {
    if (out_.error == ParseError::none) {
      out_.error = error;
      out_.error_offset = current().offset;
    }
  }
  [[nodiscard]] bool take(Kind kind) {
    if (current().kind != kind) return false;
    ++pos_;
    return true;
  }
  [[nodiscard]] bool name(std::string_view word) {
    if (current().kind != Kind::name || text() != word) return false;
    ++pos_;
    return true;
  }
  // Every expression in this restricted scalar frontend evaluates to i64;
  // infer therefore resolves to i64 without permitting dynamic values.
  [[nodiscard]] bool scalar_return_type() {
    return name("int") || name("infer");
  }
  [[nodiscard]] std::optional<std::size_t> append(Node n) {
    if (out_.nodes.size() >= (strict_ ? max_strict_nodes : max_nodes)) {
      fail(ParseError::node_limit);
      return std::nullopt;
    }
    if(strict_){
      switch(n.op){
        case Op::literal: case Op::refine: case Op::narrow: case Op::copy: case Op::sequence:
        case Op::eq: case Op::ne: case Op::lt: case Op::le: case Op::gt: case Op::ge: case Op::choose: case Op::loop_state: case Op::loop_value: case Op::loop_group: case Op::loop_read: break;
        case Op::neg:
          if(ranges_[n.left].lo==std::numeric_limits<i64>::min()){
            fail(ParseError::verify_unproven);return std::nullopt;
          }break;
        case Op::add: case Op::sub: case Op::mul: case Op::div:
          if(!ranges_[n.left].proves(ranges_[n.right],n.op)){
            fail(ParseError::verify_unproven);return std::nullopt;
          }break;
        default: fail(ParseError::verify_unsupported);return std::nullopt;
      }
    }
    Dim d{};
    Scale s{};
    Range r{};
    if(n.op==Op::literal)r=Range::exact(n.value);
    if (n.op != Op::literal) {
      const auto& lhs = dims_[n.left];
      if (n.op == Op::neg || n.op == Op::refine || n.op==Op::narrow || n.op==Op::copy || n.op==Op::loop_state) {
        d = lhs; s = scales_[n.left];
        r=n.op==Op::neg?ranges_[n.left].neg():n.op==Op::refine?Range{n.value,n.upper}:ranges_[n.left];
        if(n.op==Op::neg)r.width=ranges_[n.left].width;
        if(n.op==Op::narrow)r=r.intersect(i32_lo,i32_hi,Width::i32);
      } else if(n.op==Op::loop_read){
        const auto state=out_.nodes[n.left].states[static_cast<std::size_t>(n.value)];
        d=dims_[state];s=scales_[state];r=ranges_[state];
      } else if(n.op==Op::loop_value||n.op==Op::loop_group){
        d=lhs;s=scales_[n.left];r=ranges_[n.left];
      } else if(n.op==Op::choose){
        auto z=static_cast<std::size_t>(n.value);
        if(!dimensionless(n.left)||dims_[n.right]!=dims_[z]){fail(ParseError::unit_dimension);return std::nullopt;}
        if(scales_[n.right]!=scales_[z]){fail(ParseError::unit_scale);return std::nullopt;}
        d=dims_[n.right];s=scales_[n.right];r=ranges_[n.right].join(ranges_[z]);
      } else if(n.op==Op::eq||n.op==Op::ne||n.op==Op::lt||n.op==Op::le||n.op==Op::gt||n.op==Op::ge){
        if(lhs!=dims_[n.right]){fail(ParseError::unit_dimension);return std::nullopt;}
        if(scales_[n.left]!=scales_[n.right]){fail(ParseError::unit_scale);return std::nullopt;}
        r=Range{0,1};
      } else if(n.op==Op::sequence){
        d=dims_[n.right];s=scales_[n.right];r=ranges_[n.right];
      }
      else if (n.op == Op::bnot) {
        if (!dimensionless(n.left)) { fail(ParseError::syntax); return std::nullopt; }
      } else {
        const auto& rhs = dims_[n.right];
        if (n.op == Op::add || n.op == Op::sub) {
          if (lhs != rhs) {fail(ParseError::unit_dimension);return std::nullopt;}
          if(scales_[n.left]!=scales_[n.right]){fail(ParseError::unit_scale);return std::nullopt;}
          d = lhs; s=scales_[n.left];r=ranges_[n.left].binary(ranges_[n.right],n.op);
        } else if (n.op == Op::mul || n.op == Op::div) {
          const auto scale=scales_[n.left].mul(n.op==Op::div?scales_[n.right].inv():scales_[n.right]);
          if (!scale) { fail(ParseError::unit_limit); return std::nullopt; }
          s=*scale;r=ranges_[n.left].binary(ranges_[n.right],n.op);
          for (std::size_t i = 0; i < d.size(); ++i) {
            d[i] = lhs[i] + (n.op == Op::mul ? rhs[i] : -rhs[i]);
            if (d[i] > exponent_limit || d[i] < -exponent_limit) { fail(ParseError::syntax); return std::nullopt; }
          }
        } else if (!dimensionless(n.left) || !dimensionless(n.right)) {
          fail(ParseError::syntax); return std::nullopt;
        }
      }
    }
    const bool arithmetic=n.op==Op::add||n.op==Op::sub||n.op==Op::mul||n.op==Op::div;
    if(arithmetic){
      const auto a=ranges_[n.left],b=ranges_[n.right];
      r.width=(a.width==Width::i64||b.width==Width::i64)?Width::i64:
              (a.width==Width::i32||b.width==Width::i32)?Width::i32:Width::i64;
      if(r.width==Width::i32&&((a.width==Width::literal&&!a.within(i32_lo,i32_hi))||(b.width==Width::literal&&!b.within(i32_lo,i32_hi)))){
        fail(ParseError::i32_range);return std::nullopt;
      }
    }
    const bool check32=(arithmetic||n.op==Op::neg)&&r.width==Width::i32;
    if(check32&&strict_&&!r.within(i32_lo,i32_hi)){fail(ParseError::verify_unproven);return std::nullopt;}
    dims_.push_back(d); scales_.push_back(s); ranges_.push_back(r);
    out_.nodes.push_back(n);
    const auto i=out_.nodes.size()-1;
    if(check32)return append(Node{Op::narrow,i,0,0});
    return i;
  }
  // bit_or < bit_xor < bit_and < addition < multiplication < unary.
  // Flat precedence tiers do not consume the nesting depth budget.
  // Only actual parenthesized or unary nesting increments depth.
  [[nodiscard]] std::optional<std::size_t> expr(int depth) {
    auto result = parse_or(depth);
    if (!result) return std::nullopt;
    while (name("as")) {
      if(take(Kind::question)){fail(ParseError::refinement_cast);return std::nullopt;}
      if (current().kind == Kind::name && (text() == "int" || text() == "i64")) {
        if (!cast_target() || !scalar_cast(*result)) return std::nullopt;
        ranges_[*result].width=Width::i64;
      } else if (const auto* r=refinement_target()) {
        ++pos_;result=checked_refinement(*result,*r);
        if (!result) return std::nullopt;
      } else if (!attach(*result)) return std::nullopt;
    }
    return result;
  }
  // All admitted expressions are signed i64. These casts validate the
  // carrier and preserve the existing node, including checked arithmetic.
  // Reject every other carrier and unit; no unchecked erasure.
  [[nodiscard]] bool cast_target() {
    if (name("int") || name("i64")) return true;
    fail(ParseError::syntax);
    return false;
  }
  [[nodiscard]] std::optional<std::size_t> postfix(std::optional<std::size_t> result, bool compact = false, bool conversion = false) {
    if (!result) return std::nullopt;
    while (current().kind == Kind::double_colon || current().kind == Kind::dot ||
           (compact && current().kind == Kind::lt && pos_ > 0 &&
            tokens_[pos_-1].offset + tokens_[pos_-1].length == current().offset)) {
      const bool cast = take(Kind::double_colon);
      if (!cast && take(Kind::dot) && name("to_unit")) {
        if (!conversion || out_.nodes[*result].op!=Op::literal || !take(Kind::lt) || current().kind!=Kind::name) {
          fail(ParseError::syntax); return std::nullopt;
        }
        const Unit* target=nullptr;
        for (const auto& u:units_) if (u.name==text()) target=&u;
        if (!target) { fail(ParseError::unit_name); return std::nullopt; }
        Dim d{}; d[target->dim]=1;
        if (dims_[*result]!=d) { fail(ParseError::unit_dimension); return std::nullopt; }
        ++pos_;
        if (!take(Kind::gt)||!take(Kind::lparen)||!take(Kind::rparen)) { fail(ParseError::syntax); return std::nullopt; }
        const auto converted=scales_[*result].convert(out_.nodes[*result].value,target->scale);
        if(converted.error!=Scale::Error::none){fail(converted.error==Scale::Error::inexact?ParseError::unit_inexact:ParseError::unit_range);return std::nullopt;}
        out_.nodes[*result].value=converted.value; scales_[*result]=target->scale; ranges_[*result]=Range::exact(converted.value);
        conversion=false;compact=false;continue;
      }
      if (!take(Kind::lt)) { fail(ParseError::syntax); return std::nullopt; }
      if (cast && current().kind == Kind::name && (text() == "int" || text() == "i64")) {
        if (!cast_target() || !scalar_cast(*result)) return std::nullopt;
        ranges_[*result].width=Width::i64;
      } else if (const auto* r=cast?refinement_target():nullptr) {
        ++pos_;result=checked_refinement(*result,*r);
        if (!result) return std::nullopt;
      } else if (!attach(*result)) return std::nullopt;
      if (!take(Kind::gt)) { fail(ParseError::syntax); return std::nullopt; }
      compact = false;
    }
    return result;
  }
  [[nodiscard]] std::optional<std::size_t> parse_or(int depth) {
    auto lhs = parse_xor(depth);
    if (!lhs) return std::nullopt;
    while (take(Kind::bit_or)) {
      auto rhs = parse_xor(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{Op::bor, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  [[nodiscard]] std::optional<std::size_t> parse_xor(int depth) {
    auto lhs = parse_and(depth);
    if (!lhs) return std::nullopt;
    while (take(Kind::bit_xor)) {
      auto rhs = parse_and(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{Op::bxor, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  [[nodiscard]] std::optional<std::size_t> parse_and(int depth) {
    auto lhs = parse_shift(depth);
    if (!lhs) return std::nullopt;
    while (take(Kind::bit_and)) {
      auto rhs = parse_shift(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{Op::band, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  [[nodiscard]] std::optional<std::size_t> parse_shift(int depth) {
    auto lhs = additive(depth);
    if (!lhs) return std::nullopt;
    while (current().kind == Kind::shl || current().kind == Kind::lshr ||
           (current().kind == Kind::name && text() == "ashr")) {
      const auto op = current().kind == Kind::shl ? Op::shl :
                      current().kind == Kind::lshr ? Op::lshr : Op::ashr;
      ++pos_;
      auto rhs = additive(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{op, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  [[nodiscard]] std::optional<std::size_t> additive(int depth) {
    if (depth > max_parse_depth) { fail(ParseError::depth_limit); return std::nullopt; }
    auto lhs = term(depth);
    if (!lhs) return std::nullopt;
    while (current().kind == Kind::plus || current().kind == Kind::minus) {
      const auto op = current().kind == Kind::plus ? Op::add : Op::sub;
      ++pos_;
      auto rhs = term(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{op, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  [[nodiscard]] std::optional<std::size_t> term(int depth) {
    if (depth > max_parse_depth) { fail(ParseError::depth_limit); return std::nullopt; }
    auto lhs = unary(depth);
    if (!lhs) return std::nullopt;
    while (current().kind == Kind::star || current().kind == Kind::slash ||
           current().kind == Kind::percent) {
      Op op = Op::mul;
      if (current().kind == Kind::slash) op = Op::div;
      if (current().kind == Kind::percent) op = Op::rem;
      ++pos_;
      auto rhs = unary(depth);
      if (!rhs) return std::nullopt;
      lhs = append(Node{op, *lhs, *rhs, 0});
      if (!lhs) return std::nullopt;
    }
    return lhs;
  }
  // Only static-key, immediately projected scalar Pair factories. This parser
  // does not allocate or expose an owned product or execute runtime keys.
  [[nodiscard]] std::optional<std::size_t> pair_projection(int depth) {
    // The bounded token vector makes this conservative ambiguity scan cheap.
    // A name in declarations, qualified access or parameters must not be
    // mistaken for a builtin. Quoted text and comments are inert.
    for (std::size_t i=0;i<tokens_.size();++i) {
      const auto& t=tokens_[i];
      if(t.kind!=Kind::name || source_.substr(t.offset,t.length)!="Pair") continue;
      if(i+2>=tokens_.size() || tokens_[i+1].kind!=Kind::dot ||
         tokens_[i+2].kind!=Kind::name ||
         source_.substr(tokens_[i+2].offset,tokens_[i+2].length)!="of" ||
         (i>0 && (tokens_[i-1].kind==Kind::dot || tokens_[i-1].kind==Kind::double_colon))) {
        fail(ParseError::pair_scope); return std::nullopt;
      }
    }
    ++pos_;
    if(!take(Kind::dot)||!name("of")) {fail(ParseError::syntax);return std::nullopt;}
    if(take(Kind::lt)){
      if(name("string")){
        if(!take(Kind::comma)||!name("int")) {fail(ParseError::syntax);return std::nullopt;}
      } else {
        if(!name("int")||current().kind==Kind::comma){fail(ParseError::pair_key_type);return std::nullopt;}
      }
      if(!take(Kind::gt)){fail(ParseError::syntax);return std::nullopt;}
    }
    if(!take(Kind::lparen)){fail(ParseError::syntax);return std::nullopt;}
    if(current().kind!=Kind::quoted){fail(ParseError::pair_key_type);return std::nullopt;}
    const auto key=text();
    // The stage-0 lexer has no escape decoder. Reject escapes and control
    // bytes rather than accepting a string whose meaning cannot be proven.
    if(key.size()<2 || key.front()!='"'||key.back()!='"'){
      fail(ParseError::pair_key_unsupported);return std::nullopt;
    }
    for(unsigned char c:key.substr(1,key.size()-2)) {
      if(c<32||c>126||c==92||c==36||c==34){
        fail(ParseError::pair_key_unsupported);return std::nullopt;
      }
    }
    ++pos_;
    if(!take(Kind::comma)){fail(ParseError::syntax);return std::nullopt;}
    auto second=expr(depth+1);
    if(!second)return std::nullopt;
    // Do not erase units or accept a narrower explicitly typed value as int.
    if(!dimensionless(*second)){fail(ParseError::unit_dimension);return std::nullopt;}
    if(ranges_[*second].width==Width::i32){fail(ParseError::pair_projection);return std::nullopt;}
    if(!take(Kind::rparen)){fail(ParseError::syntax);return std::nullopt;}
    if(!take(Kind::dot)){fail(ParseError::pair_escape);return std::nullopt;}
    if(!name("second")){fail(ParseError::pair_projection);return std::nullopt;}
    // The static key has no effects; retain the checked expression DAG
    // exactly once, including runtime arithmetic error paths.
    return postfix(append(Node{Op::copy,*second,0,0}));
  }

  // Both eager int operands are retained in the verified DAG. A copy of
  // the first slot after the second evaluation selects key without rerunning it.
  [[nodiscard]] std::optional<std::size_t> entry_projection(int depth) {
    // The bounded token vector makes this conservative ambiguity scan cheap.
    // A name in declarations, qualified access or parameters must not be
    // mistaken for a builtin. Quoted text and comments are inert.
    for (std::size_t i=0;i<tokens_.size();++i) {
      const auto& t=tokens_[i];
      if(t.kind!=Kind::name || source_.substr(t.offset,t.length)!="Entry") continue;
      if(i+2>=tokens_.size() || tokens_[i+1].kind!=Kind::dot ||
         tokens_[i+2].kind!=Kind::name ||
         source_.substr(tokens_[i+2].offset,tokens_[i+2].length)!="of" ||
         (i>0 && (tokens_[i-1].kind==Kind::dot || tokens_[i-1].kind==Kind::double_colon))) {
        fail(ParseError::entry_scope); return std::nullopt;
      }
    }
    ++pos_;
    if(!take(Kind::dot)||!name("of")){fail(ParseError::syntax);return std::nullopt;}
    if(take(Kind::lt)){
      if(!name("int")||!take(Kind::comma)||!name("int")||!take(Kind::gt)){
        fail(ParseError::entry_projection);return std::nullopt;
      }
    }
    if(!take(Kind::lparen)){fail(ParseError::syntax);return std::nullopt;}
    auto first=expr(depth+1);
    if(!first)return std::nullopt;
    // Fresh copy anchors even a reused binding in source evaluation order.
    first=append(Node{Op::copy,*first,0,0});
    if(!first)return std::nullopt;
    if(!take(Kind::comma)){fail(ParseError::syntax);return std::nullopt;}
    auto second=expr(depth+1);
    if(!second)return std::nullopt;
    second=append(Node{Op::copy,*second,0,0});
    if(!second)return std::nullopt;
    if(!dimensionless(*first)||!dimensionless(*second)){
      fail(ParseError::unit_dimension);return std::nullopt;
    }
    if(ranges_[*first].width==Width::i32||ranges_[*second].width==Width::i32){
      fail(ParseError::entry_projection);return std::nullopt;
    }
    if(!take(Kind::rparen)){fail(ParseError::syntax);return std::nullopt;}
    if(!take(Kind::dot)){fail(ParseError::entry_escape);return std::nullopt;}
    if(name("value"))return postfix(append(Node{Op::sequence,*first,*second,0}));
    if(!name("key")){fail(ParseError::entry_projection);return std::nullopt;}
    auto selected=append(Node{Op::copy,*first,0,0});
    if(!selected)return std::nullopt;
    auto tail=append(Node{Op::sequence,*second,*selected,0});
    if(!tail)return std::nullopt;
    return postfix(append(Node{Op::sequence,*first,*tail,0}));
  }

  [[nodiscard]] std::optional<std::size_t> unary(int depth) {
    if (depth > max_parse_depth) { fail(ParseError::depth_limit); return std::nullopt; }
    if(current().kind==Kind::name && text()=="Pair" && pos_+1<tokens_.size() && tokens_[pos_+1].kind==Kind::dot) return pair_projection(depth);
    if(current().kind==Kind::name && text()=="Entry" && pos_+1<tokens_.size() && tokens_[pos_+1].kind==Kind::dot) return entry_projection(depth);
    if (take(Kind::lt)) {
      if (const auto* r=refinement_target()) {
        ++pos_;
        if (!take(Kind::gt)) {fail(ParseError::syntax);return std::nullopt;}
        auto operand=unary(depth+1);
        if (!operand) return std::nullopt;
        return checked_refinement(*operand,*r);
      }
      if (!cast_target() || !take(Kind::gt)) {
        fail(ParseError::syntax); return std::nullopt;
      }
      auto operand = unary(depth + 1);
      if (!operand || !scalar_cast(*operand)) return std::nullopt;
      ranges_[*operand].width=Width::i64;
      return operand;
    }
    // Restricted synchronous task expression: the same scalar DAG and
    // caller, no independent task identity, allocation or suspension.
    // Do not admit this into the strict refinement verifier yet.
    if (name("sync")) {
      if (!name("task") || strict_ || !helpers_.empty() ||
          !refinements_.empty() || !units_.empty() || !dimensions_.empty()) {
        fail(ParseError::task_unsupported); return std::nullopt;
      }
      if (take(Kind::lt)) {
        if (name("void")) { fail(ParseError::task_return_type); return std::nullopt; }
        if (!name("int") || !take(Kind::gt)) {
          fail(ParseError::task_unsupported); return std::nullopt;
        }
      }
      if (!take(Kind::lbrace)) { fail(ParseError::syntax); return std::nullopt; }
      // Prefix locals belong to this task alone. Restore the binding table
      // on every parse outcome, including failure in a nested task.
      const auto outer = bindings_.size();
      auto result = [&]() -> std::optional<std::size_t> {
        std::vector<std::size_t> initializers;
        std::vector<std::string_view> mutable_locals;
        for (;;) {
          const bool mut = name("let");
          const bool val = !mut && name("val");
          std::string_view local;
          std::optional<std::size_t> target;
          if (mut || val) {
            if (bindings_.size() >= 32 || !name("int") ||
                current().kind != Kind::name || !binding_name(text())) {
              fail(ParseError::task_unsupported); return std::nullopt;
            }
            local = text();
            ++pos_;
          } else if (current().kind == Kind::name && pos_+1 < tokens_.size() &&
                     tokens_[pos_+1].kind == Kind::equals) {
            local = text();
            if (std::find(mutable_locals.begin(),mutable_locals.end(),local)==mutable_locals.end()) {
              fail(ParseError::task_unsupported); return std::nullopt;
            }
            for (std::size_t i=outer;i<bindings_.size();++i)
              if (bindings_[i].first==local) { target=i; break; }
            if (!target) { fail(ParseError::task_unsupported); return std::nullopt; }
            ++pos_;
          } else break;
          if (initializers.size()>=64) { fail(ParseError::task_unsupported); return std::nullopt; }
          if (!take(Kind::equals)) { fail(ParseError::syntax); return std::nullopt; }
          auto value = expr(depth + 1);
          if (!value || !scalar_cast(*value) || !take(Kind::semicolon)) {
            fail(ParseError::syntax); return std::nullopt;
          }
          // The RHS sees the previous version. Earlier reads and aliases keep
          // their DAG slots, and the sequence forces even overwritten writes.
          auto slot = append(Node{Op::copy,*value,0,0});
          if (!slot) return std::nullopt;
          ranges_[*slot].width = Width::i64;
          if (target) bindings_[*target].second=*slot;
          else {
            bindings_.emplace_back(local,*slot);
            if (mut) mutable_locals.push_back(local);
          }
          initializers.push_back(*slot);
        }
        if (!name("return") && !take(Kind::caret)) {
          fail(ParseError::task_unsupported); return std::nullopt;
        }
        if (current().kind == Kind::semicolon) {
          fail(ParseError::task_return_type); return std::nullopt;
        }
        auto root = expr(depth + 1);
        if (!root || !scalar_cast(*root) || !take(Kind::semicolon) ||
            !take(Kind::rbrace)) {
          fail(ParseError::syntax); return std::nullopt;
        }
        for (auto i = initializers.rbegin(); i != initializers.rend(); ++i) {
          root = append(Node{Op::sequence,*i,*root,0});
          if (!root) return std::nullopt;
        }
        return root;
      }();
      bindings_.resize(outer);
      if (!result) return std::nullopt;
      return postfix(result);
    }
    if (current().kind == Kind::name && text() == "task") {
      fail(ParseError::task_unsupported); return std::nullopt;
    }
    if (take(Kind::plus)) return unary(depth + 1);
    if (take(Kind::tilde)) {
      auto rhs = unary(depth + 1);
      if (!rhs) return std::nullopt;
      return append(Node{Op::bnot, *rhs, 0, 0});
    }
    if (take(Kind::minus)) {
      // Literal INT64_MIN is allowed; negating any evaluated INT64_MIN
      // is a checked overflow in the subsequent evaluator/codegen.
      if (current().kind == Kind::integer && text().size() < 64) {
        // The negative range includes |INT64_MIN|. Reuse the canonical
        // decimal validator instead of comparing only the unpadded token.
        // This accepts -0009223372036854775808 consistently with other
        // decimal leading-zero literals but rejects larger magnitudes.
        const std::string signed_digits = "-" + std::string(text());
        const auto minimum = parse_source_decimal(signed_digits);
        if (minimum && *minimum == std::numeric_limits<i64>::min()) {
          ++pos_;
          return postfix(append(Node{Op::literal, 0, 0, *minimum}), true, true);
        }
      }
      auto rhs = unary(depth + 1);
      if (!rhs) return std::nullopt;
      return append(Node{Op::neg, *rhs, 0, 0});
    }
    if (take(Kind::lparen)) {
      auto inside = expr(depth + 1);
      if (!inside) return std::nullopt;
      if (!take(Kind::rparen)) { fail(ParseError::syntax); return std::nullopt; }
      return postfix(inside);
    }
    if (current().kind == Kind::integer) {
      auto n = parse_source_decimal(text());
      if (!n) { fail(ParseError::literal_out_of_range); return std::nullopt; }
      ++pos_;
      return postfix(append(Node{Op::literal, 0, 0, *n}), true, true);
    }
    if(current().kind==Kind::name){
      for(const auto& h:helpers_)if(h.name==text()){++pos_;return call(h,depth+1);}
      for(const auto& b:bindings_)if(b.first==text()){
        if(boolean_slot(b.second)){fail(ParseError::bool_operand);return std::nullopt;}
        const auto key=b.first;
        ++pos_;
        auto a=std::find_if(strict_arrays_.begin(),strict_arrays_.end(),[&](const auto& x){return x.first==key;});
        if(a!=strict_arrays_.end()){
          if(!take(Kind::lbracket)){fail(ParseError::verify_unsupported);return std::nullopt;}
          auto index=expr(depth+1);if(!index||!take(Kind::rbracket))return std::nullopt;
          if(!dimensionless(*index)){fail(ParseError::unit_dimension);return std::nullopt;}
          const auto lo=ranges_[*index].lo,hi=ranges_[*index].hi;
          if(lo<0||hi>=static_cast<i64>(a->second.size())){fail(ParseError::verify_unproven);return std::nullopt;}
          auto value=append(Node{Op::copy,a->second[static_cast<std::size_t>(hi)],0,0});if(!value)return std::nullopt;
          for(auto k=hi-1;k>=lo;--k){
            auto constant=append(Node{Op::literal,0,0,k});if(!constant)return std::nullopt;
            auto test=append(Node{Op::eq,*index,*constant,0});if(!test)return std::nullopt;
            auto selected=append(Node{Op::copy,a->second[static_cast<std::size_t>(k)],0,0});if(!selected)return std::nullopt;
            value=append(Node{Op::choose,*test,*selected,static_cast<i64>(*value)});if(!value)return std::nullopt;
          }
          // A singleton index still executes once, and remains reachable.
          if(lo==hi)value=append(Node{Op::sequence,*index,*value,0});
          return postfix(value);
        }
        // A new read node owns independent type metadata; attaching a unit
        // to this read must never mutate the original binding.
        return postfix(append(Node{Op::copy,b.second,0,0}));
      }
    }
    if(current().kind==Kind::name&&pos_+1<tokens_.size()&&tokens_[pos_+1].kind==Kind::lparen&&!refinements_.empty()){fail(ParseError::refinement_call);return std::nullopt;}
    if(current().kind==Kind::name&&binding_name(text())&&pos_+1<tokens_.size()&&tokens_[pos_+1].kind!=Kind::lparen){fail(ParseError::unbound_local);return std::nullopt;}
    fail(ParseError::syntax);
    return std::nullopt;
  }
  [[nodiscard]] bool binding_name(std::string_view n) const {
    if(n.empty()||(!(n[0]>='a'&&n[0]<='z')&&!portable_nonascii_name_start(unicode_at(n,0).value))||known_name(n))return false;
    for(auto s:{"val","let","int","i64","i32","bool","float","string","void","return","self","main","fnc","pub","define","type","where","as","and","units","in","true","false","if","else","end","begin","while","loop","sync","task","await","suspends","infer","new","match","const","mut","using","unit","dimension","_"})if(n==s)return false;
    for(const auto& b:bindings_)if(b.first==n)return false;
    for(const auto& a:strict_arrays_)if(a.first==n)return false;
    for(const auto& h:helpers_)if(h.name==n)return false;
    return true;
  }
  [[nodiscard]] std::optional<Refinement> annotation(std::string_view n) const {
    if(n=="int"||n=="i64")return Refinement{n,Dim{},Scale{},std::numeric_limits<i64>::min(),std::numeric_limits<i64>::max(),Width::i64};
    if(n=="i32")return Refinement{n,Dim{},Scale{},i32_lo,i32_hi,Width::i32};
    for(const auto& r:refinements_)if(r.name==n)return r;
    return std::nullopt;
  }
  [[nodiscard]] bool compatible(std::size_t n,const Refinement& want,ParseError category){
    if(dims_[n]!=want.dim){fail(ParseError::unit_dimension);return false;}
    if(scales_[n]!=want.scale){fail(ParseError::unit_scale);return false;}
    if(!ranges_[n].within(want.lo,want.hi)){fail(category);return false;}
    return true;
  }
  [[nodiscard]] std::optional<std::size_t> call(const Helper& h,int depth){
    // Each call gets fresh graph nodes. Parameter and initializer sequences
    // remain branch-local; do not hoist the helper DAG.
    if(depth>max_parse_depth||!take(Kind::lparen)){fail(ParseError::syntax);return std::nullopt;}
    std::vector<std::size_t> map;
    for(std::size_t i=0;i<h.params.size();++i){
      if(i&&!take(Kind::comma)){fail(ParseError::syntax);return std::nullopt;}
      if(current().kind==Kind::rparen){fail(ParseError::refinement_call);return std::nullopt;}
      auto arg=expr(depth+1);
      if(!arg||!compatible(*arg,h.params[i],ParseError::refinement_precondition)){fail(ParseError::syntax);return std::nullopt;}
      map.push_back(*arg);
    }
    if(!take(Kind::rparen)){fail(ParseError::refinement_call);return std::nullopt;}
    // Template graph has already been type-checked over the declared
    // parameter intervals. Instantiate once, with only backward edges.
    for(std::size_t i=h.params.size();i<h.nodes.size();++i){
      if(out_.nodes.size()>=(strict_?max_strict_nodes:max_nodes)){fail(ParseError::node_limit);return std::nullopt;}
      Node n=h.nodes[i];
      if(n.op!=Op::literal){
        if(n.left>=map.size()){fail(ParseError::syntax);return std::nullopt;}
        n.left=map[n.left];
        if(n.op!=Op::neg&&n.op!=Op::bnot&&n.op!=Op::refine&&n.op!=Op::narrow&&n.op!=Op::copy&&n.op!=Op::loop_state&&n.op!=Op::loop_read){
          if(n.right>=map.size()){fail(ParseError::syntax);return std::nullopt;}
          n.right=map[n.right];
        }
        if(n.op==Op::choose||n.op==Op::loop_value){if(n.value<0||static_cast<std::size_t>(n.value)>=map.size())return std::nullopt;n.value=static_cast<i64>(map[static_cast<std::size_t>(n.value)]);}
        if(n.op==Op::loop_group){
          for(auto* refs:{&n.states,&n.effects,&n.updates})for(auto& k:*refs){
            if(k>=map.size())return std::nullopt;
            k=map[k];
          }
          for(auto& x:n.exits){
            if(x.condition>=map.size())return std::nullopt;
            x.condition=map[x.condition];
            for(auto& k:x.values){if(k>=map.size())return std::nullopt;k=map[k];}
          }
        }
      }
      map.push_back(out_.nodes.size());out_.nodes.push_back(n);
      dims_.push_back(h.dims[i]);scales_.push_back(h.scales[i]);ranges_.push_back(h.ranges[i]);
    }
    if(map.empty()){fail(ParseError::syntax);return std::nullopt;}
    auto root=append(Node{Op::copy,map.back(),0,0});
    if(!root)return std::nullopt;
    dims_[*root]=h.result.dim;scales_[*root]=h.result.scale;
    ranges_[*root]=Range{h.result.lo,h.result.hi,h.result.width};
    return postfix(root);
  }
  using Facts=std::vector<std::pair<std::size_t,Range>>;
  struct Predicate{std::size_t node;Facts yes,no;};
  Facts facts() const{Facts f;for(const auto& b:bindings_){bool seen=false;for(const auto& x:f)if(x.first==b.second)seen=true;if(!seen)f.emplace_back(b.second,ranges_[b.second]);}return f;}
  void set_facts(const Facts& f){for(const auto& [index,r]:f)ranges_[index]=r;}
  static Facts join_facts(const Facts& a,const Facts& b){
    auto out=a;for(std::size_t i=0;i<a.size();++i)out[i].second=a[i].second.join(b[i].second);return out;
  }
  std::optional<Predicate> predicate(const Facts& incoming,int depth){
    if(depth>16){fail(ParseError::depth_limit);return std::nullopt;}
    set_facts(incoming);
    // The strict scalar lane admits only typed predicate negation, never
    // integer truthiness. Parenthesize comparisons to retain unary precedence.
    if(take(Kind::bang)||take(Kind::unicode_not)){
      if(current().kind!=Kind::lparen && current().kind!=Kind::bang &&
          current().kind!=Kind::unicode_not &&
          !(current().kind==Kind::name&&(text()=="true"||text()=="false"||boolean_binding(text()).has_value()))){
        fail(current().kind==Kind::integer?ParseError::bool_operand:ParseError::verify_unsupported);return std::nullopt;
      }
      auto p=predicate(incoming,depth+1);if(!p)return std::nullopt;
      auto zero=append(Node{Op::literal,0,0,0});if(!zero)return std::nullopt;
      auto neg=append(Node{Op::eq,p->node,*zero,0});if(!neg)return std::nullopt;
      return Predicate{*neg,std::move(p->no),std::move(p->yes)};
    }
    if(current().kind==Kind::name&&(text()=="true"||text()=="false")){
      const bool truth=name("true");if(!truth)(void)name("false");
      auto node=append(Node{Op::literal,0,0,truth?1:0});if(!node)return std::nullopt;
      return Predicate{*node,incoming,incoming};
    }
    if(current().kind==Kind::name){
      if(auto b=boolean_binding(text())){++pos_;auto node=append(Node{Op::copy,*b,0,0});
        if(!node)return std::nullopt;
        return Predicate{*node,incoming,incoming};}
    }
    if(take(Kind::lparen)){auto p=guard_or(incoming,depth+1);if(!p||!take(Kind::rparen))return std::nullopt;return p;}
    auto a=expr(0);if(!a)return std::nullopt;
    Op op,inverse;
    if(take(Kind::equal_equal)){op=Op::eq;inverse=Op::ne;}
    else if(take(Kind::not_equal)){op=Op::ne;inverse=Op::eq;}
    else if(current().kind==Kind::lt||current().kind==Kind::gt){
      bool lower=take(Kind::lt);if(!lower)++pos_;
      bool inclusive=current().kind==Kind::equals&&tokens_[pos_-1].offset+tokens_[pos_-1].length==current().offset;
      if(inclusive)++pos_;
      op=lower?(inclusive?Op::le:Op::lt):(inclusive?Op::ge:Op::gt);
      inverse=lower?(inclusive?Op::gt:Op::ge):(inclusive?Op::lt:Op::le);
    }else{fail(ParseError::verify_unsupported);return std::nullopt;}
    auto b=expr(0);if(!b)return std::nullopt;
    auto yes=incoming,no=incoming;
    if(ranges_[*b].lo==ranges_[*b].hi){
      for(std::size_t i=0;i<incoming.size();++i)if(out_.nodes[*a].op==Op::copy&&
        out_.nodes[*a].left==incoming[i].first&&dims_[*a]==dims_[incoming[i].first]&&scales_[*a]==scales_[incoming[i].first]){
        yes[i].second=incoming[i].second.restrict(op,ranges_[*b].lo);
        no[i].second=incoming[i].second.restrict(inverse,ranges_[*b].lo);
      }
    }
    auto c=append(Node{op,*a,*b,0});if(!c)return std::nullopt;
    return Predicate{*c,std::move(yes),std::move(no)};
  }
  std::optional<Predicate> guard_and(const Facts& incoming,int depth){
    auto p=predicate(incoming,depth);if(!p)return std::nullopt;
    while(take(Kind::logical_and)){
      auto q=predicate(p->yes,depth);if(!q)return std::nullopt;
      auto zero=append(Node{Op::literal,0,0,0});if(!zero)return std::nullopt;
      auto c=append(Node{Op::choose,p->node,q->node,static_cast<i64>(*zero)});if(!c)return std::nullopt;
      p=Predicate{*c,std::move(q->yes),join_facts(p->no,q->no)};
    }return p;
  }
  std::optional<Predicate> guard_or(const Facts& incoming,int depth){
    auto p=guard_and(incoming,depth);if(!p)return std::nullopt;
    while(take(Kind::logical_or)){
      auto q=guard_and(p->no,depth);if(!q)return std::nullopt;
      auto one=append(Node{Op::literal,0,0,1});if(!one)return std::nullopt;
      auto c=append(Node{Op::choose,p->node,*one,static_cast<i64>(q->node)});if(!c)return std::nullopt;
      p=Predicate{*c,join_facts(p->yes,q->yes),std::move(q->no)};
    }return p;
  }
  // Ordinary terminal conditional returns reuse the checked Boolean guard
  // grammar and lazy choose node; no integer truthiness or new mutable lane.
  std::optional<std::size_t> scalar_return_expression(){
    if(!strict_){
      unsigned nesting=0;bool conditional=false;
      for(auto i=pos_;i<tokens_.size();++i){
        const auto k=tokens_[i].kind;
        if(k==Kind::lparen||k==Kind::lbracket)++nesting;
        else if(k==Kind::rparen||k==Kind::rbracket){if(!nesting)break;--nesting;}
        else if(!nesting&&(k==Kind::semicolon||k==Kind::rbrace||k==Kind::end))break;
        else if(!nesting&&k==Kind::question){
          if(i>pos_&&tokens_[i-1].kind==Kind::name&&source_.substr(tokens_[i-1].offset,tokens_[i-1].length)=="as")break;
          conditional=true;break;
        }
      }
      if(conditional){
        const auto incoming=facts();auto p=guard_or(incoming,0);
        if(!p||!take(Kind::question)){fail(ParseError::syntax);return std::nullopt;}
        auto y=expr(0);if(!y||!take(Kind::colon)){fail(ParseError::syntax);return std::nullopt;}
        auto n=expr(0);if(!n)return std::nullopt;
        set_facts(incoming);
        return append(Node{Op::choose,p->node,*y,static_cast<i64>(*n)});
      }
    }
    return expr(0);
  }
  std::optional<std::size_t> returning(int depth){
    if(depth>16){fail(ParseError::depth_limit);return std::nullopt;}
    if(!strict_&&name("if")){
      const auto incoming=facts();auto p=guard_or(incoming,0);
      if(!p||!name("then")){fail(ParseError::syntax);return std::nullopt;}
      auto y=returning(depth+1);if(!y||!name("else")){fail(ParseError::syntax);return std::nullopt;}
      auto n=returning(depth+1);if(!n||!name("fi")){fail(ParseError::syntax);return std::nullopt;}
      set_facts(incoming);
      return append(Node{Op::choose,p->node,*y,static_cast<i64>(*n)});
    }
    if(strict_&&name("if")){
      auto incoming=facts();bool old=guarded_;guarded_=true;
      auto p=guard_or(incoming,0);if(!p||!take(Kind::lbrace))return std::nullopt;
      set_facts(p->yes);auto y=returning(depth+1);if(!y||!take(Kind::rbrace))return std::nullopt;
      // Snapshot path-dependent result metadata before restoring input facts.
      auto yr=ranges_[*y];auto yd=dims_[*y];auto ys=scales_[*y];
      set_facts(p->no);bool alternate=name("else");if(alternate&&!take(Kind::lbrace))return std::nullopt;
      auto n=returning(depth+1);if(!n||(alternate&&!take(Kind::rbrace)))return std::nullopt;
      auto nr=ranges_[*n];set_facts(incoming);guarded_=old;
      if(yd!=dims_[*n]||ys!=scales_[*n]){fail(ParseError::unit_dimension);return std::nullopt;}
      auto result=append(Node{Op::choose,p->node,*y,static_cast<i64>(*n)});
      if(result)ranges_[*result]=yr.join(nr);
      return result;
    }
    if(!name("return")&&!take(Kind::caret)){fail(ParseError::syntax);return std::nullopt;}
    auto root=scalar_return_expression();if(!root||!take(Kind::semicolon))return std::nullopt;return root;
  }
  using MutableTypes=std::vector<std::pair<std::string_view,Refinement>>;
  using ContinueBranch=std::function<std::optional<std::size_t>(int)>;
  bool strict_loop(MutableTypes mutable_types,std::vector<std::size_t>& effects,int depth){
    if(depth>16){fail(ParseError::depth_limit);return false;}
    auto brace=pos_;
    while(brace<tokens_.size()&&tokens_[brace].kind!=Kind::lbrace)++brace;
    if(brace==tokens_.size()){fail(ParseError::verify_unsupported);return false;}
    unsigned nesting=1;std::vector<std::string_view> writes;
    for(auto k=brace+1;k<tokens_.size()&&nesting;++k){
      const auto& t=tokens_[k];
      if(t.kind==Kind::lbrace)++nesting;
      else if(t.kind==Kind::rbrace)--nesting;
      else if(t.kind==Kind::name&&k+1<tokens_.size()&&tokens_[k+1].kind==Kind::equals){
        const auto key=source_.substr(t.offset,t.length);
        if(std::any_of(mutable_types.begin(),mutable_types.end(),[&](auto& x){return x.first==key;})&&
           std::find(writes.begin(),writes.end(),key)==writes.end())writes.push_back(key);
      }
    }
    if(nesting){fail(ParseError::verify_unsupported);return false;}
    const auto outer=bindings_;
    Node group{Op::loop_group};
    for(auto key:writes){
      auto binding=std::find_if(bindings_.begin(),bindings_.end(),[&](auto& x){return x.first==key;});
      auto want=std::find_if(mutable_types.begin(),mutable_types.end(),[&](auto& x){return x.first==key;})->second;
      if(binding==bindings_.end()||!compatible(binding->second,want,ParseError::refinement_binding))return false;
      auto state=append(Node{Op::loop_state,binding->second,0,0});if(!state)return false;
      binding->second=*state;ranges_[*state]=Range{want.lo,want.hi,want.width};group.states.push_back(*state);
    }
    // A state-free loop still needs a private cache region and group result.
    if(group.states.empty()){
      auto zero=append(Node{Op::literal,0,0,0});if(!zero)return false;
      auto state=append(Node{Op::loop_state,*zero,0,0});if(!state)return false;
      group.states.push_back(*state);
    }
    group.left=group.states.front();
    const bool old=guarded_;guarded_=true;
    const auto header=facts();
    bool has_break=false;
    auto p=guard_or(header,0);if(!p||!take(Kind::lbrace))return false;
    group.right=p->node;set_facts(p->yes);
    while(!take(Kind::rbrace)){
      if(current().kind==Kind::name&&(text()=="if"||text()=="break"||text()=="continue")){
        const bool guarded=name("if");
        std::optional<Predicate> edge;
        if(guarded){edge=guard_or(facts(),0);if(!edge||!take(Kind::lbrace))return false;}
        else{
          auto yes=append(Node{Op::literal,0,0,1});if(!yes)return false;
          edge=Predicate{*yes,facts(),facts()};
        }
        const bool breaking=name("break");
        if(!breaking&&!name("continue")){fail(ParseError::verify_unsupported);return false;}
        if(!take(Kind::semicolon))return false;
        if(guarded){if(!take(Kind::rbrace))return false;}
        else if(current().kind!=Kind::rbrace){fail(ParseError::verify_unsupported);return false;}
        set_facts(edge->yes);
        LoopExit x{group.effects.size(),edge->node,breaking,{}};
        for(auto key:writes){
          auto binding=std::find_if(bindings_.begin(),bindings_.end(),[&](auto& b){return b.first==key;});
          auto want=std::find_if(mutable_types.begin(),mutable_types.end(),[&](auto& b){return b.first==key;})->second;
          if(binding==bindings_.end()||!compatible(binding->second,want,ParseError::refinement_binding))return false;
          x.values.push_back(binding->second);
        }
        if(writes.empty())x.values.push_back(group.states.front());
        group.exits.push_back(std::move(x));has_break|=breaking;
        set_facts(edge->no);continue;
      }
      if(name("while")){
        if(!strict_loop(mutable_types,group.effects,depth+1))return false;
        continue;
      }
      const bool imm=name("val"),mut=!imm&&name("let");
      if(imm||mut){
        if(bindings_.size()>=32||current().kind!=Kind::name){fail(ParseError::syntax);return false;}
        auto key=text();++pos_;std::optional<Refinement> want;
        if(take(Kind::colon)){
          if(current().kind!=Kind::name){fail(ParseError::syntax);return false;}
          want=annotation(text());++pos_;if(!want)return false;
        }else if(current().kind!=Kind::equals){
          want=annotation(key);
          if(!want||current().kind!=Kind::name){fail(ParseError::syntax);return false;}
          key=text();++pos_;
        }
        if(mut&&!want){fail(ParseError::verify_unsupported);return false;}
        if(current().kind==Kind::semicolon){fail(ParseError::missing_initializer);return false;}
        if(!binding_name(key)||!take(Kind::equals))return false;
        auto value=expr(0);if(!value||!take(Kind::semicolon))return false;
        if(want&&!compatible(*value,*want,ParseError::refinement_binding))return false;
        auto version=append(Node{Op::copy,*value,0,0});if(!version)return false;
        if(want)ranges_[*version].width=want->width;
        else if(ranges_[*version].width==Width::literal)ranges_[*version].width=Width::i64;
        bindings_.emplace_back(key,*version);group.effects.push_back(*version);
        if(mut)mutable_types.emplace_back(key,*want);
        continue;
      }
      if(current().kind!=Kind::name||pos_+1>=tokens_.size()||tokens_[pos_+1].kind!=Kind::equals){fail(ParseError::verify_unsupported);return false;}
      const auto key=text();pos_+=2;
      auto binding=std::find_if(bindings_.begin(),bindings_.end(),[&](auto& x){return x.first==key;});
      auto decl=std::find_if(mutable_types.begin(),mutable_types.end(),[&](auto& x){return x.first==key;});
      if(binding==bindings_.end()||decl==mutable_types.end()){fail(ParseError::verify_unsupported);return false;}
      auto value=expr(0);if(!value||!take(Kind::semicolon)||!compatible(*value,decl->second,ParseError::refinement_binding))return false;
      auto version=append(Node{Op::copy,*value,0,0});if(!version)return false;
      ranges_[*version].width=decl->second.width;binding->second=*version;group.effects.push_back(*version);
    }
    for(auto key:writes){
      auto binding=std::find_if(bindings_.begin(),bindings_.end(),[&](auto& x){return x.first==key;});
      auto want=std::find_if(mutable_types.begin(),mutable_types.end(),[&](auto& x){return x.first==key;})->second;
      if(binding==bindings_.end()||!compatible(binding->second,want,ParseError::refinement_binding))return false;
      group.updates.push_back(binding->second);
    }
    if(writes.empty())group.updates.push_back(group.states.front());
    set_facts(has_break?header:p->no);guarded_=old;bindings_=outer;
    auto loop=append(std::move(group));if(!loop)return false;
    effects.push_back(*loop);
    for(std::size_t j=0;j<writes.size();++j){
      auto projection=append(Node{Op::loop_read,*loop,0,static_cast<i64>(j)});if(!projection)return false;
      auto binding=std::find_if(bindings_.begin(),bindings_.end(),[&](auto& x){return x.first==writes[j];});
      binding->second=*projection;effects.push_back(*projection);
    }
    return true;
  }
  std::optional<std::size_t> strict_sequence(std::optional<Refinement> result,MutableTypes mutable_types,
      std::vector<std::size_t> effects,int depth,ContinueBranch on_exit={}){
    if(depth>16){fail(ParseError::depth_limit);return std::nullopt;}
    for(;;){
      if(strict_&&name("while")){
        if(!strict_loop(mutable_types,effects,depth+1))return std::nullopt;
        continue;
      }
      // Ordinary fixed locals share the checked val lowering. Do not
      // expand the strict refinement or mutable SSA admission boundary.
      const bool ordinary_type_first=!strict_&&current().kind==Kind::name&&text()=="int";
      const bool ordinary_const=!strict_&&!ordinary_type_first&&name("const");
      if(ordinary_const) (void)name("mut");
      const bool immutable=ordinary_type_first||ordinary_const||name("val");
      const bool mut=!immutable&&strict_&&name("let");
      if(!immutable&&!mut){
        if(!strict_||current().kind!=Kind::name||pos_+1>=tokens_.size()||tokens_[pos_+1].kind!=Kind::equals)break;
        const auto n=text();pos_+=2;
        auto binding=std::find_if(bindings_.begin(),bindings_.end(),[&](const auto& x){return x.first==n;});
        auto decl=std::find_if(mutable_types.begin(),mutable_types.end(),[&](const auto& x){return x.first==n;});
        if(binding==bindings_.end()||decl==mutable_types.end()){fail(ParseError::verify_unsupported);return std::nullopt;}
        auto value=expr(0);
        if(!value||!take(Kind::semicolon)||!compatible(*value,decl->second,ParseError::refinement_binding))return std::nullopt;
        auto version=append(Node{Op::copy,*value,0,0});if(!version)return std::nullopt;
        ranges_[*version].width=decl->second.width;
        binding->second=*version;effects.push_back(*version);
        continue;
      }
      if(bindings_.size()>=(strict_?32:max_ordinary_bindings)||current().kind!=Kind::name){fail(ParseError::syntax);return std::nullopt;}
      auto n=text();++pos_;
      if(!strict_&&immutable&&n=="bool"){
        if(current().kind!=Kind::name){fail(ParseError::syntax);return std::nullopt;}
        n=text();++pos_;
        if(std::any_of(bindings_.begin(),bindings_.end(),[&](const auto& b){return b.first==n;})){fail(ParseError::duplicate_local);return std::nullopt;}
        if(!binding_name(n)||!take(Kind::equals)){fail(ParseError::syntax);return std::nullopt;}
        const auto incoming=facts();auto p=guard_or(incoming,0);
        if(!p||!take(Kind::semicolon)){fail(ParseError::syntax);return std::nullopt;}
        set_facts(incoming);boolean_slots_.push_back(p->node);
        bindings_.emplace_back(n,p->node);effects.push_back(p->node);continue;
      }
      std::optional<Refinement> want;
      if(take(Kind::colon)||(!strict_&&name("as"))){
        if(current().kind!=Kind::name){fail(ParseError::syntax);return std::nullopt;}
        want=annotation(text());++pos_;if(!want){fail(ParseError::syntax);return std::nullopt;}
      }else if(current().kind!=Kind::equals){
        want=annotation(n);
        if(!want||current().kind!=Kind::name){fail(ParseError::syntax);return std::nullopt;}
        n=text();++pos_;
      }
      if(mut&&!want){fail(ParseError::verify_unsupported);return std::nullopt;}
      if(strict_&&current().kind==Kind::semicolon){fail(ParseError::missing_initializer);return std::nullopt;}
      if(std::any_of(bindings_.begin(),bindings_.end(),[&](const auto& b){return b.first==n;})){fail(ParseError::duplicate_local);return std::nullopt;}
      if(!binding_name(n)||!take(Kind::equals)){fail(ParseError::syntax);return std::nullopt;}
      if(strict_&&name("Array")){
        if(mut||want){fail(ParseError::verify_unsupported);return std::nullopt;}
        if(!take(Kind::dot)||!name("of")||!take(Kind::lparen))return std::nullopt;
        std::vector<std::size_t> cells;
        do{
          if(cells.size()>=16){fail(ParseError::unit_limit);return std::nullopt;}
          auto cell=expr(0);if(!cell)return std::nullopt;
          if(!dimensionless(*cell)){fail(ParseError::unit_dimension);return std::nullopt;}
          auto snapshot=append(Node{Op::copy,*cell,0,0});if(!snapshot)return std::nullopt;
          cells.push_back(*snapshot);effects.push_back(*snapshot);
        }while(take(Kind::comma));
        if(!take(Kind::rparen)||!take(Kind::semicolon))return std::nullopt;
        bindings_.emplace_back(n,cells.front());
        strict_arrays_.emplace_back(n,std::move(cells));continue;
      }
      auto value=expr(0);
      if(!value||!take(Kind::semicolon)){fail(ParseError::syntax);return std::nullopt;}
      if(want&&!compatible(*value,*want,ParseError::refinement_binding)){
        fail(ParseError::syntax);return std::nullopt;
      }
      if(strict_){
        value=append(Node{Op::copy,*value,0,0});if(!value)return std::nullopt;
      }
      if(want)ranges_[*value].width=want->width;
      else if(ranges_[*value].width==Width::literal)ranges_[*value].width=Width::i64;
      bindings_.emplace_back(n,*value);effects.push_back(*value);
      if(mut)mutable_types.emplace_back(n,*want);
    }

    std::optional<std::size_t> root;
    if(strict_&&current().kind==Kind::name&&(text()=="break"||text()=="continue")){fail(ParseError::verify_unsupported);return std::nullopt;}
    if(strict_&&current().kind==Kind::rbrace&&on_exit){
      root=on_exit(depth);
    }else if(strict_&&name("if")){
      // Split the bounded continuation into the selected native DAG arm.
      // Do not evaluate untaken assignments or leak branch-local declarations.
      const auto incoming=bindings_;const auto incoming_facts=facts();
      const bool old=guarded_;guarded_=true;
      auto p=guard_or(incoming_facts,0);if(!p)return std::nullopt;
      const auto close=[&](std::size_t open)->std::optional<std::size_t>{
        if(open>=tokens_.size()||tokens_[open].kind!=Kind::lbrace)return std::nullopt;
        unsigned nesting=0;
        for(auto k=open;k<tokens_.size();++k){
          if(tokens_[k].kind==Kind::lbrace)++nesting;
          else if(tokens_[k].kind==Kind::rbrace&&--nesting==0)return k;
        }return std::nullopt;
      };
      const auto yes_open=pos_;const auto yc=close(yes_open);
      if(!yc){fail(ParseError::syntax);return std::nullopt;}
      const auto next=*yc+1;
      const bool has_else=next<tokens_.size()&&tokens_[next].kind==Kind::name&&
        source_.substr(tokens_[next].offset,tokens_[next].length)=="else";
      const auto no_open=next+1;const auto nc=has_else?close(no_open):yc;
      if(!nc){fail(ParseError::syntax);return std::nullopt;}
      const auto resume=*nc+1;std::optional<std::size_t> tail_end;
      ContinueBranch continuation=[&](int d)->std::optional<std::size_t>{
        const auto saved=pos_;
        auto scoped=incoming;
        for(auto& item:scoped){
          auto it=std::find_if(bindings_.begin(),bindings_.end(),[&](const auto& x){return x.first==item.first;});
          if(it==bindings_.end()){fail(ParseError::verify_unsupported);return std::nullopt;}
          item.second=it->second;
        }
        bindings_=std::move(scoped);pos_=resume;
        auto tail=strict_sequence(result,mutable_types,{},d+1,on_exit);
        if(!tail)return std::nullopt;
        if(tail_end&&*tail_end!=pos_){fail(ParseError::verify_unsupported);return std::nullopt;}
        tail_end=pos_;pos_=saved;return tail;
      };
      pos_=yes_open+1;set_facts(p->yes);
      auto y=strict_sequence(result,mutable_types,{},depth+1,continuation);
      if(!y||pos_!=*yc){fail(ParseError::syntax);return std::nullopt;}
      const auto yr=ranges_[*y];const auto yd=dims_[*y];const auto ys=scales_[*y];
      bindings_=incoming;set_facts(p->no);
      pos_=has_else?no_open+1:resume;
      auto n=strict_sequence(result,mutable_types,{},depth+1,has_else?continuation:on_exit);
      if(!n||(has_else&&pos_!=*nc)){fail(ParseError::syntax);return std::nullopt;}
      if(!has_else){
        if(tail_end&&*tail_end!=pos_){fail(ParseError::verify_unsupported);return std::nullopt;}
        tail_end=pos_;
      }
      const auto nr=ranges_[*n];
      pos_=tail_end.value_or(resume);
      bindings_=incoming;set_facts(incoming_facts);guarded_=old;
      if(yd!=dims_[*n]||ys!=scales_[*n]){fail(ParseError::unit_dimension);return std::nullopt;}
      root=append(Node{Op::choose,p->node,*y,static_cast<i64>(*n)});
      if(root)ranges_[*root]=yr.join(nr);
    }else{
      root=returning(depth);
    }
    if(!root)return std::nullopt;
    for(auto i=effects.rbegin();i!=effects.rend();++i){
      root=append(Node{Op::sequence,*i,*root,0});
      if(!root)return std::nullopt;
    }
    return root;
  }
  [[nodiscard]] std::optional<std::size_t> binding_body(std::optional<Refinement> result=std::nullopt,bool end_delimited=false){
    std::vector<std::size_t> effects;
    for(const auto& binding:bindings_)effects.push_back(binding.second);
    auto root=strict_sequence(result,{},std::move(effects),0);
    if(!root||(result?!compatible(*root,*result,ParseError::refinement_return):!scalar_cast(*root))||!(end_delimited?name("end"):take(Kind::rbrace))){
      fail(ParseError::syntax);return std::nullopt;
    }
    return root;
  }
  struct Contract {Op op; std::optional<std::size_t> parameter; std::optional<i64> bound;};
  std::optional<Op> contract_op(){
    if(name("eq")||take(Kind::equal_equal))return Op::eq;
    if(current().kind!=Kind::lt&&current().kind!=Kind::gt){fail(ParseError::refinement_contract);return std::nullopt;}
    const bool lower=take(Kind::lt);if(!lower)++pos_;
    const bool inclusive=current().kind==Kind::equals&&tokens_[pos_-1].offset+tokens_[pos_-1].length==current().offset;
    if(inclusive)++pos_;
    return lower?(inclusive?Op::le:Op::lt):(inclusive?Op::ge:Op::gt);
  }
  std::optional<i64> contract_bound(const Refinement& want){
    const auto n=signed_bound();if(!n){fail(ParseError::refinement_contract);return std::nullopt;}
    Dim d{};Scale scale{};const bool dot=take(Kind::dot);
    if(dot||current().kind==Kind::lt){
      if(!take(Kind::lt)||current().kind!=Kind::name)return std::nullopt;
      const Unit* u=nullptr;for(const auto& candidate:units_)if(candidate.name==text())u=&candidate;
      if(!u){fail(ParseError::unit_name);return std::nullopt;}
      d[u->dim]=1;scale=u->scale;++pos_;if(!take(Kind::gt))return std::nullopt;
    }
    if(d!=want.dim){fail(ParseError::unit_dimension);return std::nullopt;}
    if(scale!=want.scale){fail(ParseError::unit_scale);return std::nullopt;}
    return n;
  }
  std::optional<std::vector<Contract>> contracts(std::vector<Refinement>& params,const Refinement& result){
    std::vector<Contract> out;unsigned count=0;
    for(;;){
      const bool require=name("requires");if(!require&&!name("ensures"))break;
      if(++count>16||current().kind!=Kind::name){fail(ParseError::refinement_contract);return std::nullopt;}
      auto n=text();++pos_;auto op=contract_op();if(!op)return std::nullopt;
      auto param_index=[&](std::string_view key)->std::optional<std::size_t>{
        for(std::size_t i=0;i<params.size();++i)if(bindings_[i].first==key)return i;
        return std::nullopt;
      };
      if(require){
        auto i=param_index(n);if(!i){fail(ParseError::refinement_contract);return std::nullopt;}
        auto& p=params[*i];auto v=contract_bound(p);if(!v)return std::nullopt;
        if(*op==Op::eq){p.lo=std::max(p.lo,*v);p.hi=std::min(p.hi,*v);}
        else if(*op==Op::lt){auto b=checked_sub(*v,1);if(!b){fail(ParseError::refinement_empty);return std::nullopt;}p.hi=std::min(p.hi,*b);}
        else if(*op==Op::le)p.hi=std::min(p.hi,*v);
        else if(*op==Op::gt){auto b=checked_add(*v,1);if(!b){fail(ParseError::refinement_empty);return std::nullopt;}p.lo=std::max(p.lo,*b);}
        else if(*op==Op::ge)p.lo=std::max(p.lo,*v);
        if(p.lo>p.hi){fail(ParseError::refinement_empty);return std::nullopt;}
        ranges_[bindings_[*i].second]=Range{p.lo,p.hi,p.width};
      }else{
        if(n!="result"){fail(ParseError::refinement_contract);return std::nullopt;}
        if(current().kind==Kind::name){
          auto i=param_index(text());if(!i){fail(ParseError::refinement_contract);return std::nullopt;}
          if(params[*i].dim!=result.dim){fail(ParseError::unit_dimension);return std::nullopt;}
          if(params[*i].scale!=result.scale){fail(ParseError::unit_scale);return std::nullopt;}
          ++pos_;out.push_back(Contract{*op,i,std::nullopt});
        }else{
          auto v=contract_bound(result);if(!v)return std::nullopt;
          out.push_back(Contract{*op,std::nullopt,v});
        }
      }
    }return out;
  }
  // Parameters occupy the initial placeholder nodes. Never treat a
  // placeholder's stored zero as a real constant during proof.
  bool positive_constant(std::size_t i,std::size_t params,unsigned depth)const{
    if(i<params||i>=out_.nodes.size()||depth>=64)return false;
    const auto& n=out_.nodes[i];
    if(n.op==Op::literal)return n.value>=1;
    if(n.op==Op::copy)return positive_constant(n.left,params,depth+1);
    return false;
  }
  bool relation(std::size_t i,std::size_t param,std::size_t params,bool nonnegative,unsigned depth)const{
    if(i>=out_.nodes.size()||depth>=64)return false;
    if(i<params)return i==param;
    const auto& n=out_.nodes[i];
    if(n.op==Op::copy||n.op==Op::refine||n.op==Op::narrow)return relation(n.left,param,params,nonnegative,depth+1);
    if(n.op==Op::sequence)return relation(n.right,param,params,nonnegative,depth+1);
    if(n.op==Op::div&&nonnegative)return relation(n.left,param,params,nonnegative,depth+1)&&positive_constant(n.right,params,depth+1);
    return false;
  }
  bool prove_contracts(const std::vector<Contract>& checks,std::size_t root,const std::vector<Refinement>& params){
    const auto r=ranges_[root];
    for(const auto& c:checks){
      bool proved=false;
      if(c.bound){
        const auto n=*c.bound;
        switch(c.op){case Op::eq:proved=r.lo==n&&r.hi==n;break;case Op::lt:proved=r.hi<n;break;case Op::le:proved=r.hi<=n;break;case Op::gt:proved=r.lo>n;break;case Op::ge:proved=r.lo>=n;break;default:break;}
      }else if(c.parameter){
        const auto i=*c.parameter;const auto p=params[i];
        switch(c.op){case Op::le:proved=r.hi<=p.lo||relation(root,i,params.size(),p.lo>=0,0);break;case Op::lt:proved=r.hi<p.lo;break;case Op::gt:proved=r.lo>p.hi;break;case Op::ge:proved=r.lo>=p.hi;break;case Op::eq:proved=r.lo==r.hi&&p.lo==p.hi&&r.lo==p.lo;break;default:break;}
      }
      if(!proved){fail(ParseError::refinement_postcondition);return false;}
    }return true;
  }
  [[nodiscard]] bool helper_declarations(){
    while(current().kind==Kind::name&&text()=="fnc"&&pos_+1<tokens_.size()){
      const auto& next=tokens_[pos_+1];
      const auto spelling=source_.substr(next.offset,next.length);
      if(spelling=="main")break;
      // Keep existing type-first entrypoint spellings on the legacy path.
      if((spelling=="int"||spelling=="infer")&&pos_+2<tokens_.size()&&tokens_[pos_+2].kind==Kind::left_arrow)break;
      if((strict_&&refinements_.empty())||helpers_.size()>=16)return false;
      ++pos_;
      if(current().kind!=Kind::name||!binding_name(text()))return false;
      const auto fname=text();++pos_;
      if(!take(Kind::lparen))return false;
      std::vector<Refinement> params;
      if(!take(Kind::rparen)){
        for(;;){
          if(params.size()>=8||current().kind!=Kind::name)return false;
          const auto ty=annotation(text());++pos_;
          if(!ty||current().kind!=Kind::name||!binding_name(text()))return false;
          const auto param=text();++pos_;
          const auto n=append(Node{Op::literal,0,0,0});
          if(!n)return false;
          dims_[*n]=ty->dim;scales_[*n]=ty->scale;ranges_[*n]=Range{ty->lo,ty->hi,ty->width};
          bindings_.emplace_back(param,*n);params.push_back(*ty);
          if(take(Kind::rparen))break;
          if(!take(Kind::comma))return false;
        }
      }
      if(!take(Kind::arrow)&&!take(Kind::colon))return false;
      if(current().kind!=Kind::name)return false;
      auto result=annotation(text());++pos_;
      if(!result)return false;
      const auto contract_start=pos_;
      auto checks=contracts(params,*result);
      if(!checks)return false;
      const bool contracted=pos_!=contract_start;
      bool end_delimited=false;
      if(!take(Kind::lbrace)){
        if(take(Kind::double_colon))end_delimited=true;
        else if(current().kind==Kind::name&&text()=="begin"){
          const auto& previous=tokens_[pos_-1];
          const auto start=previous.offset+previous.length;
          if(contracted&&(start>current().offset||
              source_.substr(start,current().offset-start).find('\n')==std::string_view::npos)){
            fail(ParseError::syntax);return false;
          }
          ++pos_;end_delimited=true;
        }else{fail(ParseError::syntax);return false;}
      }
      auto root=binding_body(*result,end_delimited);
      if(!root||!prove_contracts(*checks,*root,params))return false;
      out_.root=*root;
      if(!validate_ast(out_))return false;
      helpers_.push_back(Helper{fname,std::move(params),*result,std::move(out_.nodes),std::move(dims_),std::move(scales_),std::move(ranges_)});
      out_.nodes.clear();out_.root=0;dims_.clear();scales_.clear();ranges_.clear();bindings_.clear();strict_arrays_.clear();boolean_slots_.clear();
    }
    return true;
  }
  [[nodiscard]] std::optional<std::size_t> body(std::vector<std::string_view>& aliases) {
    std::optional<std::size_t> root;
    // Empty or bare-return void tasks have no observable effects.
    // Reject any body requiring local assignment, cleanup or a continuation.
    while (name("do")) {
      if (!name("sync") || !name("task") || strict_ ||
          !helpers_.empty() || !refinements_.empty() || !units_.empty() || !dimensions_.empty()) {
        fail(ParseError::task_unsupported); return std::nullopt;
      }
      if (take(Kind::lt)) {
        if (!name("void")) { fail(ParseError::task_return_type); return std::nullopt; }
        if (!take(Kind::gt)) { fail(ParseError::syntax); return std::nullopt; }
      }
      if (!take(Kind::lbrace)) { fail(ParseError::syntax); return std::nullopt; }
      if (name("return") || take(Kind::caret)) {
        if (!take(Kind::semicolon)) {
          fail(ParseError::task_return_type); return std::nullopt;
        }
      }
      if (!take(Kind::rbrace)) {
        fail(ParseError::task_unsupported); return std::nullopt;
      }
      (void)take(Kind::semicolon);
    }
    while (name("using")) {
      if (root || aliases.size() == max_trace_depth ||
          !name("telemetry") || !take(Kind::dot) || !name("span") ||
          !take(Kind::lparen) || current().kind != Kind::quoted) {
        fail(ParseError::telemetry);
        return std::nullopt;
      }
      const auto operation = text().substr(1, text().size() - 2);
      ++pos_;
      if (!valid_operation(operation) || !take(Kind::rparen) ||
          !name("as") || current().kind != Kind::name) {
        fail(ParseError::telemetry);
        return std::nullopt;
      }
      const auto alias = text();
      ++pos_;
      if (alias == "_" || alias == "self" || !take(Kind::lbrace)) {
        fail(ParseError::telemetry);
        return std::nullopt;
      }
      for (const auto& active_alias : aliases) {
        if (active_alias == alias) {
          fail(ParseError::telemetry);
          return std::nullopt;
        }
      }
      out_.trace_steps.emplace_back(operation);
      aliases.push_back(alias);
      root = body(aliases);
      aliases.pop_back();
      if (out_.error != ParseError::none) return std::nullopt;
      if (!root) out_.trace_steps.emplace_back(std::nullopt);
    }
    if (name("return") || take(Kind::caret)) {
      const auto previous_size = out_.nodes.size();
      auto value = scalar_return_expression();
      if (!value || !scalar_cast(*value) || !take(Kind::semicolon)) {
        fail(ParseError::syntax);
        return std::nullopt;
      }
      if (root) {
        // Parse the unreachable fallback after a terminal scope, then
        // discard those unreachable scalar nodes.
        out_.nodes.resize(previous_size);
        dims_.resize(previous_size); scales_.resize(previous_size); ranges_.resize(previous_size);
      } else {
        root = value;
      }
    }
    if (!take(Kind::rbrace)) fail(ParseError::syntax);
    return root;
  }
  [[nodiscard]] bool expected(Kind kind) {
    if (take(kind)) return true;
    fail(ParseError::pointer_profile);
    return false;
  }
  [[nodiscard]] bool expected(std::string_view word) {
    if (name(word)) return true;
    fail(ParseError::pointer_profile);
    return false;
  }
  [[nodiscard]] std::optional<std::string> local_id() {
    if (current().kind != Kind::name) {
      fail(ParseError::pointer_profile);
      return std::nullopt;
    }
    std::string name_value(text());
    ++pos_;
    if (name_value.size() > 96 ||
        name_value == "main" || name_value == "int" ||
        name_value == "Pointer" || name_value == "let" ||
        name_value == "val" || name_value == "rt" ||
        name_value == "ref" || name_value == "deref" ||
        name_value == "unref" || name_value == "return" ||
        name_value == "fnc" || name_value == "pub") {
      fail(ParseError::pointer_profile);
      return std::nullopt;
    }
    return name_value;
  }
  [[nodiscard]] bool local_op(std::string_view op) {
    if (name("rt")) return expected(op);
    const auto begin = current().offset;
    if (!expected(op)) return false;
    // Even whitespace or comments between operation and ! are rejected;
    // source offsets are retained by the *canonical* lexer.
    if (current().kind != Kind::bang ||
        current().offset != begin + op.size()) {
      fail(ParseError::pointer_profile);
      return false;
    }
    ++pos_;
    return true;
  }


  [[nodiscard]] bool placement_point() {
    using K=PointPlacement::Kind;
    PointPlacement plan;
    std::vector<std::string> names;
    auto bad=[&](){fail(ParseError::pointer_profile);return false;};
    auto w=[&](std::string_view v){return expected(v);};
    auto k=[&](Kind v){return expected(v);};
    auto op=[&](std::string_view v){return current().kind==Kind::name&&text()==v&&local_op(v);};
    auto number=[&]()->std::optional<i64>{
      const bool negative=take(Kind::minus);
      if(current().kind!=Kind::integer)return std::nullopt;
      const auto raw=text();
      auto v=negative?parse_decimal_i64(std::string("-")+std::string(raw)):parse_decimal_i64(raw);
      if(v)++pos_;
      return v;
    };
    auto identifier=[&]()->std::optional<std::string>{
      auto n=local_id();if(!n)return std::nullopt;
      const std::string_view reserved[]={"s","p","x","y","mut","new","at","alloc","destroy","free","define","struct","with","constructor","self","null","true","false","await","suspends","susp","if","else","end","while","for","class","actor","spawn","task","select","defer","recover","copy","borrow","take","share","drop","move","pin","import","export","use","namespace","module","const","unsafe","to","empty","void","panic","trap","throw","raise"};
      if(n->empty()||(*n)[0]<'a'||(*n)[0]>'z'||n->size()>64)return std::nullopt;
      for(char c:*n)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return std::nullopt;
      for(auto v:reserved)if(*n==v)return std::nullopt;
      return n;
    };
    if(!w("define")||!w("struct")||!w("Point")||!k(Kind::lbrace)||
       !w("int")||!w("x")||!k(Kind::semicolon)||!w("int")||!w("y")||!k(Kind::semicolon)||
       !k(Kind::rbrace)||!w("with")||!k(Kind::lbrace)||!w("constructor")||
       !k(Kind::lparen)||!w("int")||!w("x")||!k(Kind::comma)||!w("int")||!w("y")||
       !k(Kind::rparen)||!k(Kind::lbrace)||!w("self")||!k(Kind::dot)||!w("x")||!k(Kind::equals)||!w("x")||!k(Kind::semicolon)||
       !w("self")||!k(Kind::dot)||!w("y")||!k(Kind::equals)||!w("y")||!k(Kind::semicolon)||!k(Kind::rbrace)||!k(Kind::rbrace)||
       !w("pub")||!w("fnc")||!w("main")||!k(Kind::lparen)||!k(Kind::rparen)||!k(Kind::colon)||!w("int")||!k(Kind::lbrace)||
       !w("let")||!w("Storage")||!k(Kind::lt)||!w("Point")||!k(Kind::gt)||!w("s")||!k(Kind::equals)||
       !op("alloc")||!w("Point")||!k(Kind::semicolon)||
       !w("let")||!w("mut")||!w("RawPointer")||!k(Kind::lt)||!w("Point")||!k(Kind::gt)||!w("p")||
       !k(Kind::equals)||!w("RawPointer")||!k(Kind::dot)||!w("empty")||!k(Kind::lt)||!w("Point")||!k(Kind::gt)||
       !k(Kind::lparen)||!k(Kind::rparen)||!k(Kind::semicolon))return bad();
    while(!(current().kind==Kind::name&&text()=="return")){
      if(plan.steps.size()>=32)return bad();
      PointPlacement::Step s{K::free};
      if(name("new")){
        s.kind=K::construct;
        if(!w("Point")||!k(Kind::lparen))return bad();
        auto x=number();if(!x||!k(Kind::comma))return bad();
        auto y=number();if(!y||!k(Kind::rparen)||!w("at")||!w("s"))return bad();
        s.x=*x;s.y=*y;
      }else if(current().kind==Kind::name&&text()=="ref"){
        if(!op("ref")||!w("p")||!w("to")||!w("s"))return bad();
        s.kind=K::bind;
      }else if(name("val")){
        auto n=identifier();
        if(!n||std::find(names.begin(),names.end(),*n)!=names.end()||
           !k(Kind::equals)||!k(Kind::lparen)||!op("deref")||!w("p")||!k(Kind::rparen)||!k(Kind::dot))return bad();
        s.kind=K::copy_field;
        if(name("x"))s.field=0;else{if(!w("y"))return bad();s.field=1;}
        names.push_back(*n);
      }else if(current().kind==Kind::name&&text()=="unref"){
        if(!op("unref")||!w("p"))return bad();
        s.kind=K::detach;
      }else if(current().kind==Kind::name&&text()=="destroy"){
        if(!op("destroy")||!w("s"))return bad();
        s.kind=K::destroy;
      }else if(current().kind==Kind::name&&text()=="free"){
        if(!op("free")||!w("s"))return bad();
        s.kind=K::free;
      }else return bad();
      if(!k(Kind::semicolon))return bad();
      plan.steps.push_back(s);
    }
    if(!w("return"))return bad();
    do{
      auto n=identifier();if(!n||plan.result.size()>=2)return bad();
      auto i=std::find(names.begin(),names.end(),*n);
      if(i==names.end())return bad();
      plan.result.push_back(static_cast<std::size_t>(i-names.begin()));
    }while(take(Kind::plus));
    if(!k(Kind::semicolon)||!k(Kind::rbrace)||current().kind!=Kind::end)return bad();
    auto result=verify_point_placement(plan);if(!result)return bad();
    auto root=append(Node{Op::literal,0,0,*result});if(!root)return bad();
    out_.root=*root;out_.point_placement=std::move(plan);return true;
  }
  [[nodiscard]] bool reusable_pointer() {
    ReusableSlot plan;
    std::vector<std::string> names;
    auto reject_slot = [&]() { fail(ParseError::pointer_profile); return false; };
    auto slot_id = [&]() -> std::optional<std::string> {
      auto id = local_id();
      if (!id) return std::nullopt;
      const std::string_view reserved[] = {"RawPointer", "mut", "null", "true", "false",
        "to", "empty", "await", "suspends", "actor", "class", "struct", "define",
        "new", "copy", "borrow", "drop", "take", "share", "freeze", "weak", "pin",
        "if", "else", "end", "while", "for", "task", "select", "defer", "recover"};
      if (!((*id)[0] >= 'a' && (*id)[0] <= 'z') && (*id)[0] != '_') {
        fail(ParseError::pointer_profile); return std::nullopt;
      }
      for (auto word : reserved) if (*id == word) {
        fail(ParseError::pointer_profile); return std::nullopt;
      }
      return id;
    };
    auto index_of = [&](const std::string& id) {
      return static_cast<std::size_t>(std::find(names.begin(), names.end(), id) - names.begin());
    };
    auto integer = [&]() -> std::optional<i64> {
      if (current().kind != Kind::integer) return std::nullopt;
      auto value = parse_decimal_i64(text());
      if (value) ++pos_;
      return value;
    };
    if (!expected("let")) return false;
    while (!name("mut")) {
      if (plan.owners.size() >= 16) return reject_slot();
      auto owner = slot_id();
      if (!owner || index_of(*owner) != names.size() ||
          !expected(Kind::colon) || !expected("int") || !expected(Kind::equals))
        return reject_slot();
      const auto value = integer();
      if (!value || !expected(Kind::semicolon)) return reject_slot();
      names.push_back(*owner); plan.owners.push_back(*value);
      if (!expected("let")) return false;
    }
    if (!expected("RawPointer") || !expected(Kind::lt) ||
        !expected("int") || !expected(Kind::gt)) return false;
    auto handle = slot_id();
    if (!handle || index_of(*handle) != names.size() ||
        !expected(Kind::equals) || !expected("RawPointer") || !expected(Kind::dot) ||
        !expected("empty") || !expected(Kind::lt) || !expected("int") ||
        !expected(Kind::gt) || !expected(Kind::lparen) || !expected(Kind::rparen) ||
        !expected(Kind::semicolon)) return reject_slot();
    std::optional<std::size_t> bound;
    while (!(current().kind == Kind::name && text() == "return")) {
      if (plan.steps.size() >= 32) return reject_slot();
      if (current().kind == Kind::name && text() == "ref") {
        if (bound || !local_op("ref") || !expected(*handle) || !expected("to"))
          return reject_slot();
        auto owner = slot_id();
        if (!owner || index_of(*owner) >= plan.owners.size()) return reject_slot();
        bound = index_of(*owner);
        plan.steps.push_back({ReusableSlot::Kind::bind, *bound});
      } else if (name("val")) {
        auto result = slot_id();
        if (!result || *result == *handle || index_of(*result) != names.size() ||
            !bound || !expected(Kind::equals) || current().kind != Kind::name ||
            text() != "deref" || !local_op("deref") || !expected(*handle))
          return reject_slot();
        plan.steps.push_back({ReusableSlot::Kind::read, *bound});
        names.push_back(*result);
      } else if (current().kind == Kind::name && text() == "unref") {
        if (!bound || !local_op("unref") || !expected(*handle)) return reject_slot();
        plan.steps.push_back({ReusableSlot::Kind::detach, 0});
        bound.reset();
      } else return reject_slot();
      if (!expected(Kind::semicolon)) return false;
    }
    if (bound || !expected("return")) return reject_slot();
    bool subtract = false;
    do {
      if (plan.result.size() >= 32) return reject_slot();
      if (current().kind == Kind::integer) {
        auto v = integer();
        if (!v) return reject_slot();
        plan.result.push_back({true, *v, subtract});
      } else {
        auto id = slot_id();
        if (!id || index_of(*id) == names.size()) return reject_slot();
        plan.result.push_back({false, static_cast<i64>(index_of(*id)), subtract});
      }
      if (take(Kind::plus)) subtract = false;
      else if (take(Kind::minus)) subtract = true;
      else break;
    } while (true);
    if (!expected(Kind::semicolon) || !expected(Kind::rbrace) ||
        current().kind != Kind::end) return reject_slot();
    const auto value = verify_reusable_slot(plan);
    if (!value) return reject_slot();
    // This closed profile permits full compile-time scalar promotion.
    // The retained typed proof is checked again by validate_ast before emission.
    auto root = append(Node{Op::literal, 0, 0, *value});
    if (!root) return false;
    out_.root = *root;
    out_.reusable_slot = std::move(plan);
    return true;
  }
  [[nodiscard]] bool local_pointer() {
    auto owner = local_id();
    if (!owner || !expected(Kind::colon) ||
        !expected("int") || !expected(Kind::equals))
      return false;
    if (current().kind != Kind::integer) {
      fail(ParseError::pointer_profile);
      return false;
    }
    auto initial = parse_decimal_i64(text());
    if (!initial) {
      fail(ParseError::literal_out_of_range);
      return false;
    }
    ++pos_;
    if (!expected(Kind::semicolon) || !expected("val") ||
        !expected("Pointer") || !expected(Kind::lt) ||
        !expected("int") || !expected(Kind::gt))
      return false;
    auto handle = local_id();
    if (!handle) return false;
    if (*handle == *owner) {
      fail(ParseError::pointer_profile);
      return false;
    }
    if (!expected(Kind::equals) || !local_op("ref") ||
        !expected(*owner) || !expected(Kind::semicolon) ||
        !expected("val"))
      return false;
    auto copy = local_id();
    if (!copy) return false;
    if (*copy == *owner || *copy == *handle) {
      fail(ParseError::pointer_profile);
      return false;
    }
    if (!expected(Kind::equals) || !local_op("deref") ||
        !expected(*handle) || !expected(Kind::semicolon) ||
        !local_op("unref") || !expected(*handle) ||
        !expected(Kind::semicolon) || !expected("return") ||
        !expected(*copy) || !expected(Kind::semicolon) ||
        !expected(Kind::rbrace) || current().kind != Kind::end) {
      fail(ParseError::pointer_profile);
      return false;
    }
    // The proof is closed-world: exactly one readonly copy and one unref
    // precede the only return. No branch, call, actor, await, handle copy,
    // second function or operation is accepted before guest emission.
    out_.local = BorrowedLocal{*owner, *handle, *copy, *initial};
    auto root = append(Node{Op::literal, 0, 0, *initial});
    if (!root) return false;
    out_.root = *root;
    return true;
  }
public:
  Parser(std::string_view source, Lexed lexed, bool strict = false)
      : source_(source), tokens_(std::move(lexed.tokens)), strict_(strict) {
    if (!lexed.ok()) {
      out_.error = lexed.error == LexError::ownership_bang_unsupported
          ? ParseError::ownership_bang_unsupported
          : lexed.error == LexError::suspension_unsupported
              ? ParseError::suspension_unsupported : lexed.error == LexError::source_bidi
                  ? ParseError::source_bidi : ParseError::lexical;
      out_.error_offset = lexed.error_offset;
      return;
    }
    // The scalar lane has no host output authority or bounded stdout runtime.
    // Identify complete endpoint calls in tokens, not in comments or literals.
    const auto is = [&](std::size_t i, Kind kind, std::string_view word = {}) {
      return i < tokens_.size() && tokens_[i].kind == kind &&
        (word.empty() || source_.substr(tokens_[i].offset, tokens_[i].length) == word);
    };
    for (std::size_t i = 0; i + 5 < tokens_.size(); ++i) {
      if (i > 0 && (is(i - 1, Kind::dot) || is(i - 1, Kind::double_colon))) continue;
      const bool endpoint = (is(i, Kind::name, "stdio") && is(i + 2, Kind::name, "out")) ||
                            (is(i, Kind::name, "process") && is(i + 2, Kind::name, "stdout"));
      if (endpoint && is(i + 1, Kind::dot) && is(i + 3, Kind::dot) &&
          is(i + 4, Kind::name, "write") && is(i + 5, Kind::lparen)) {
        out_.error = ParseError::stdout_profile_unsupported;
        out_.error_offset = tokens_[i].offset;
        return;
      }
    }
    // Neither this restricted scalar profile nor its verified variant can
    // construct an actor. Reject executable spawn heads before scalar IR,
    // without treating inert comments, quotes or property names as keywords.
    for (std::size_t i = 0; i + 1 < tokens_.size(); ++i) {
      const auto& token = tokens_[i];
      if (token.kind == Kind::name && tokens_[i + 1].kind == Kind::name &&
          (i == 0 || tokens_[i - 1].kind != Kind::dot) &&
          source_.substr(token.offset, token.length) == "spawn") {
        out_.error = ParseError::spawn_options_unsupported;
        out_.error_offset = token.offset;
        return;
      }
    }
  }
  [[nodiscard]] Parsed parse() {
    if (out_.error != ParseError::none) return std::move(out_);
    if(!strict_&&current().kind==Kind::name&&text()=="define"&&tokens_.size()>2&&
       tokens_[1].kind==Kind::name&&source_.substr(tokens_[1].offset,tokens_[1].length)=="struct"&&
       tokens_[2].kind==Kind::name&&source_.substr(tokens_[2].offset,tokens_[2].length)=="Point"){
      (void)placement_point();return std::move(out_);
    }
    if (!declarations() || !refinement_declarations()) { fail(ParseError::syntax); return std::move(out_); }
    if(strict_&&refinements_.empty()){fail(ParseError::verify_unsupported);return std::move(out_);}
    if(!helper_declarations()){fail(ParseError::syntax);return std::move(out_);}
    const bool public_entry=name("pub");
    const bool fnc = name("fnc");
    if(public_entry&&fnc&&!refinements_.empty()&&current().kind==Kind::name&&text()!="main"&&text()!="int"&&text()!="infer"){fail(ParseError::refinement_export);return std::move(out_);}
    bool signature_ok = false;
    if (scalar_return_type()) {
      // Prefix int/infer return type, with or without fnc.
      signature_ok = take(Kind::left_arrow) && name("main") &&
          take(Kind::lparen) && take(Kind::rparen);
    } else if (fnc && name("main") && take(Kind::lparen) &&
               take(Kind::rparen)) {
      // Accept colon return type, arrow return type, or block arrow.
      if (take(Kind::colon)) {
        signature_ok = scalar_return_type();
        if (signature_ok) (void)take(Kind::arrow);
      } else if (take(Kind::arrow)) {
        signature_ok = scalar_return_type();
      }
    }
    if (!signature_ok || !take(Kind::lbrace)) {
      fail(ParseError::syntax);
      return std::move(out_);
    }
    if (!strict_ && helpers_.empty() && refinements_.empty() && public_entry && fnc &&
        pos_ == 8 && current().kind == Kind::name && text() == "let") {
      bool slot_token = false;
      for (std::size_t i = pos_; i < tokens_.size(); ++i)
        if (tokens_[i].kind == Kind::name &&
            source_.substr(tokens_[i].offset, tokens_[i].length) == "RawPointer") slot_token = true;
      if (slot_token) {
        (void)reusable_pointer();
        return std::move(out_);
      }
    }
    // Only the isolated no-declaration local pointer program is eligible.
    // Never divert normal bindings, refinements, helpers or strict verification.
    if (!strict_ && helpers_.empty() && refinements_.empty() &&
        (pos_ == 7 || pos_ == 8) && current().kind == Kind::name && text() == "let") {
      bool pointer_token = false;
      for (std::size_t i = pos_; i < tokens_.size(); ++i)
        if (tokens_[i].kind == Kind::name && source_.substr(tokens_[i].offset, tokens_[i].length) == "Pointer") pointer_token = true;
      if (pointer_token) {
        ++pos_;
        (void)local_pointer();
        return std::move(out_);
      }
    }
    std::vector<std::string_view> aliases;
    auto root = strict_||!helpers_.empty()||(current().kind==Kind::name&&(text()=="val"||text()=="const"||text()=="int"||text()=="if"||(!refinements_.empty()&&text()=="let"))) ? binding_body() : body(aliases);
    if (!root || current().kind != Kind::end) {
      fail(ParseError::syntax);
      return std::move(out_);
    }
    out_.root = *root;
    return std::move(out_);
  }
};

[[nodiscard]] inline Parsed parse_source(std::string_view src) {
  auto lexed = tokenize(src);
  if (!lexed.ok()) return Parser(src, std::move(lexed)).parse();
  // Preserve the existing constructor diagnostic from #51 without
  // replacing current-main lexer/shift, cast, telemetry or AST semantics.
  const auto is = [&](std::size_t i, Kind kind, std::string_view value = {}) {
    return i < lexed.tokens.size() && lexed.tokens[i].kind == kind &&
      (value.empty() || src.substr(lexed.tokens[i].offset,
                                  lexed.tokens[i].length) == value);
  };
  for (std::size_t i = 0; i < lexed.tokens.size(); ++i) {
    if (!is(i, Kind::name, "Mutex") || !is(i + 1, Kind::lt)) continue;
    if (i > 0 && (is(i - 1, Kind::dot) || is(i - 1, Kind::double_colon)))
      continue; // no qualified-name authority for this intrinsic
    std::size_t depth = 0, close = lexed.tokens.size();
    for (std::size_t j = i + 1; j < lexed.tokens.size(); ++j) {
      if (is(j, Kind::lt)) ++depth;
      else if (is(j, Kind::gt)) {
        if (depth == 0) break;
        if (--depth == 0) { close = j; break; }
      } else if (!is(j, Kind::name)) break; // not a supported type-only spelling
    }
    if (close == lexed.tokens.size() || !is(close + 1, Kind::lparen) ||
        close == i + 2) continue;
    Parsed denied;
    denied.error = i > 0 && is(i - 1, Kind::name, "new")
      ? ParseError::lock_unsupported : ParseError::new_required;
    denied.error_offset = lexed.tokens[i].offset;
    return denied;
  }
  return Parser(src, std::move(lexed)).parse();
}
[[nodiscard]] inline Parsed parse_verified_strict_i64(std::string_view src) {
  return Parser(src, tokenize(src, max_strict_tokens), true).parse();
}
}  // namespace atto::forge::scalar
