// Independent native HOST-only oracle against the exact Forge Git source blob.
// Deliberately NOT a compiler or compiled Atto guest evidence.
#include "atto/checked_i64.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <cctype>

using atto::forge::i64;
using namespace atto::forge;
struct Fault { enum Kind { overflow, zero }; Kind kind; };
struct Parser {
  std::string_view text;
  std::size_t pos=0;
  void skip(){ while(pos<text.size() && text[pos]==' ') ++pos; }
  bool eat(char ch){ skip(); if(pos<text.size()&&text[pos]==ch){++pos;return true;}return false; }
  i64 value(){
    skip();
    auto start=pos;
    if (pos<text.size() && text[pos]=='-') ++pos;
    auto digits=pos;
    while(pos<text.size()&&std::isdigit((unsigned char)text[pos]))++pos;
    if(pos>digits){ i64 v;auto s=text.substr(start,pos-start); auto [p,err]=std::from_chars(s.data(),s.data()+s.size(),v);if(err!=std::errc()||p!=s.data()+s.size())throw std::runtime_error("bad literal");return v; }
    pos=start;
    while(pos<text.size()&&text[pos]>='a'&&text[pos]<='z')++pos;
    if(pos==start)throw std::runtime_error("bad name");
    auto name=text.substr(start,pos-start);
    if(!eat('('))throw std::runtime_error("bad call");
    std::vector<i64> xs;
    if(!eat(')')){do{xs.push_back(value());}while(eat(','));if(!eat(')'))throw std::runtime_error("missing )");}
    std::optional<i64> out;
    if(name=="sum"&&xs.size()==2)out=checked_add(xs[0],xs[1]);
    else if(name=="subtract"&&xs.size()==2)out=checked_sub(xs[0],xs[1]);
    else if(name=="multiply"&&xs.size()==2)out=checked_mul(xs[0],xs[1]);
    else if(name=="opposite"&&xs.size()==1)out=checked_neg(xs[0]);
    else if(name=="remainder"&&xs.size()==2){if(xs[1]==0)throw Fault{Fault::zero};out=checked_rem(xs[0],xs[1]);}
    else throw std::runtime_error("unknown function");
    if(!out)throw Fault{Fault::overflow};
    return *out;
  }
};
int main(int argc,char**argv){
 if(argc!=2)throw std::runtime_error("usage: adapter FILE");
 std::ifstream in(argv[1]); if(!in)throw std::runtime_error("no corpus");
 std::string line;int good=0,over=0,zero=0,rows=0;
 while(std::getline(in,line)){
  std::vector<std::string> parts;std::size_t p=0;
  while(true){auto n=line.find('|',p);parts.push_back(line.substr(p,n==std::string::npos?std::string::npos:n-p));if(n==std::string::npos)break;p=n+1;}
  if(parts.size()!=4)throw std::runtime_error("invalid TSV");
  ++rows;Parser parser{parts[2]};
  try {auto result=parser.value();if(parser.pos!=parser.text.size())throw std::runtime_error("trailing input");if(parts[1]!="value"||std::to_string(result)!=parts[3])throw std::runtime_error("wrong value " + parts[0]);++good;}
  catch(Fault err){if(err.kind==Fault::overflow&&parts[1]=="overflow"&&parts[3]=="ORES_I64_OVERFLOW")++over;
   else if(err.kind==Fault::zero&&parts[1]=="zero"&&parts[3]=="DIVISION_BY_ZERO")++zero;
   else throw std::runtime_error("wrong fault " + parts[0]);}
 }
 if(rows!=15||good!=7||over!=7||zero!=1)throw std::runtime_error("wrong result count");
 std::cout<<"HOST_HEADER_ADAPTER_PASS "<<rows<<"/15 value="<<good<<" overflow="<<over<<" zero="<<zero<<"\n";
}
