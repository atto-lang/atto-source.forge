#pragma once
// Initial direct scalar named-stage source profile using the canonical lexer.
// Only a complete standalone main with a literal upstream is admitted.
#include "atto/closed_recursive_stage.hpp"
#include "atto/decimal_i64.hpp"
#include "atto/scalar_lexer.hpp"
#include <algorithm>
#include <iterator>
#include <utility>
#include <string_view>

namespace atto::forge::recursive {
using scalar::Kind;
using scalar::Token;
struct SourceStage {
  Stage body;
  i64 input=0;
};
struct Admission {
  bool selected=false;
  std::optional<SourceStage> stage;
  std::size_t offset=0;
};
class NamedStageParser {
  std::string_view source_;
  const std::vector<Token>& tokens_;
  std::size_t pos_=0;
  std::string_view self_,param_;
  Stage body_;
  const Token& current() const {return tokens_[pos_];}
  std::string_view word() const {return source_.substr(current().offset,current().length);}
  bool take(Kind k){if(current().kind!=k||k==Kind::end)return false;++pos_;return true;}
  bool name(std::string_view s){if(current().kind!=Kind::name||word()!=s)return false;++pos_;return true;}
  static bool reserved(std::string_view s){
    constexpr std::string_view words[]={"_","main","fnc","routine","pub","int","void","infer","return","match","over","on","end","when","if","else","let","val","const","mut","true","false","trap","throw","raise","panic","await","spawn","actor","class","type","struct","enum","module","define","new","make","rt","copy","take","borrow","share","suspends"};
    return std::find(std::begin(words),std::end(words),s)!=std::end(words);
  }
  std::optional<std::string_view> identifier(){
    if(current().kind!=Kind::name||reserved(word()))return std::nullopt;
    const auto s=word();++pos_;return s;
  }
  std::optional<i64> integer(){
    const bool minus=take(Kind::minus);
    if(current().kind!=Kind::integer)return std::nullopt;
    std::string value=minus?"-":"";
    value+=word();++pos_;
    return parse_decimal_i64(value);
  }
  std::optional<std::size_t> append(Node n){
    if(body_.nodes.size()>=max_nodes)return std::nullopt;
    body_.nodes.push_back(n);return body_.nodes.size()-1;
  }
  std::optional<std::size_t> atom(std::size_t depth){
    if(depth>max_depth)return std::nullopt;
    if(current().kind==Kind::integer||(current().kind==Kind::minus&&tokens_[pos_+1].kind==Kind::integer)){
      const auto n=integer();if(!n)return std::nullopt;
      return append({Op::literal,0,0,*n});
    }
    if(take(Kind::minus)){auto n=atom(depth+1);if(!n)return {};return append({Op::neg,*n});}
    if(take(Kind::plus))return atom(depth+1);
    if(take(Kind::lparen)){auto n=expression(depth+1,0);if(!n||!take(Kind::rparen))return {};return n;}
    if(current().kind!=Kind::name)return {};
    const auto n=word();++pos_;
    if(n==param_)return append({Op::arg});
    if(n!=self_||!take(Kind::lparen))return {};
    auto v=expression(depth+1,0);
    if(!v||!take(Kind::rparen))return {};
    return append({Op::self,*v});
  }
  std::optional<std::size_t> expression(std::size_t depth,int minimum){
    if(depth>max_depth)return {};
    auto left=atom(depth);if(!left)return {};
    for(;;){
      Op op=Op::add;int precedence=0;
      switch(current().kind){
        case Kind::plus:op=Op::add;precedence=1;break;
        case Kind::minus:op=Op::sub;precedence=1;break;
        case Kind::star:op=Op::mul;precedence=2;break;
        case Kind::slash:op=Op::div;precedence=2;break;
        default:return left;
      }
      if(precedence<minimum)return left;
      ++pos_;auto right=expression(depth+1,precedence+1);
      if(!right)return {};
      left=append({op,*left,*right});if(!left)return {};
    }
  }
public:
  NamedStageParser(std::string_view s,const std::vector<Token>& ts):source_(s),tokens_(ts){}
  std::size_t offset() const{return current().offset;}
  std::optional<SourceStage> parse(){
    if(!name("pub")||!name("fnc")||!name("main")||!take(Kind::lparen)||!take(Kind::rparen)||
       !(take(Kind::colon)||take(Kind::arrow))||!name("int")||!take(Kind::lbrace)||
       !(name("return")||take(Kind::caret)))return {};
    const auto input=integer();
    if(!input||current().kind!=Kind::bit_or||tokens_[pos_+1].kind!=Kind::gt||
       current().offset+current().length!=tokens_[pos_+1].offset)return {};
    pos_+=2;
    if(!name("fnc"))return {};
    const auto self=identifier();if(!self||!take(Kind::lparen)||!name("int"))return {};
    const auto param=identifier();
    if(!param||*param==*self||!take(Kind::rparen)||!(take(Kind::colon)||take(Kind::arrow))||
       !name("int")||!take(Kind::lbrace)||!(name("return")||take(Kind::caret)))return {};
    self_=*self;param_=*param;
    if(name("match")){
      const auto subject=expression(0,0);if(!subject||!name("over"))return {};
      body_.subject=*subject;
      while(!name("end")){
        if(body_.arms.size()>=64||!name("on"))return {};
        std::optional<i64> key;
        if(!name("_")){key=integer();if(!key)return {};}
        if(!take(Kind::arrow))return {};
        auto value=expression(0,0);if(!value)return {};
        body_.arms.push_back({key,*value});
        if(!take(Kind::comma)&&!(current().kind==Kind::name&&word()=="end"))return {};
      }
    }else{
      auto value=expression(0,0);if(!value)return {};
      auto subject=append({Op::literal,0,0,0});if(!subject)return {};
      body_.subject=*subject;body_.arms.push_back({std::nullopt,*value});
    }
    (void)take(Kind::semicolon);
    if(!take(Kind::rbrace))return {};
    (void)take(Kind::semicolon);
    if(!take(Kind::rbrace)||current().kind!=Kind::end||!validate(body_))return {};
    return SourceStage{std::move(body_),*input};
  }
};
// Never fall back to the ordinary scalar parser after selecting this profile.
// Lexical errors remain the canonical lexer's responsibility.
[[nodiscard]] inline Admission admit(std::string_view source,const scalar::Lexed& lexed){
  if(!lexed.ok())return {};
  for(std::size_t i=0;i+2<lexed.tokens.size();++i){
    const auto& a=lexed.tokens[i];const auto& b=lexed.tokens[i+1];const auto& c=lexed.tokens[i+2];
    if(a.kind==Kind::bit_or&&b.kind==Kind::gt&&c.kind==Kind::name&&source.substr(c.offset,c.length)=="fnc"){
      NamedStageParser parser(source,lexed.tokens);
      auto stage=parser.parse();
      return {true,std::move(stage),parser.offset()};
    }
  }
  return {};
}
} // namespace atto::forge::recursive
