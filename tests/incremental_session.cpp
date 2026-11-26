#include "compiler/frontend.hpp"
#include <stdexcept>
#include <fstream>
#include <iostream>
static void require(bool ok){if(!ok)throw std::runtime_error("incremental check failed");}
int main(int argc,char**argv){using namespace oresforge; if(argc!=3)return 2;
 IncrementalSession s;const std::string source="fnc main(int x): int { return x+1; }",edit="fnc main(int x): int { return x+2; }";
 auto a=s.build(source,Arch::X86_64,true);require(!a.reused);require(a.assembly==emit_runtime_asm(parse_runtime_source(source),Arch::X86_64));
 auto b=s.build(source,Arch::X86_64);require(b.reused&&b.assembly==a.assembly);std::ofstream(argv[1])<<b.assembly;
 auto c=s.build(edit,Arch::X86_64);require(!c.reused&&c.assembly==emit_runtime_asm(parse_runtime_source(edit),Arch::X86_64));std::ofstream(argv[2])<<c.assembly;
 bool rejected=false;try{(void)s.build("fnc main(int x): int { return missing; }",Arch::X86_64);}catch(const std::runtime_error&){rejected=true;}require(rejected);require(s.build(edit,Arch::X86_64).reused);
 auto arm=s.build(edit,Arch::ARM64);require(!arm.reused&&arm.assembly!=c.assembly);require(s.build(edit,Arch::ARM64).reused);std::cout<<"INCREMENTAL_FORGE|exact-runtime-asm|arch-fenced|rollback\n";}
