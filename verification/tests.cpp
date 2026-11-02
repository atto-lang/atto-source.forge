#include "compiler/frontend.hpp"
#include "compiler/call_native.hpp"
#include "compiler/ir.hpp"
#include "codegen/native.hpp"
#include "runtime/actor.hpp"
#include "jit/tiered.hpp"
#include "generated_numeric_i64_cases.hpp"
#include "generated_numeric_overflow_cases.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <utility>
namespace {
void ensure(bool yes,const char* message) {
  if (!yes) throw std::runtime_error(message);
}
void reject(const std::function<void()>& fn) {
  bool caught=false;
  try { fn(); } catch (const std::exception&) { caught=true; }
  ensure(caught,"expected rejection");
}
void reject_diagnostic(const std::function<void()>& fn,std::string_view diagnostic) {
  try {
    fn();
  } catch (const std::exception& error) {
    if (std::string_view(error.what()).find(diagnostic)==std::string_view::npos)
      throw std::runtime_error("source rejected for wrong reason: "+
                               std::string(error.what()));
    return;
  }
  throw std::runtime_error("malformed source admitted unexpectedly");
}
oresforge::Program source(const std::string& expr) {
  return oresforge::parse_source("define pub fnc main() : int { return "+expr+"; }");
}
}
int main() {
  try {
    using namespace oresforge;
    // Cross-backend i64 positive controls run against the real Forge parser,
    // verified IR and checked interpreter. These are not Python-only data tests.
    for (const auto& [expression, expected] : kPortableNumericalCases) {
      ensure(interpret(source(expression))==expected,"numeric parity i64 source");
    }
    // Real compiler/IR/interpreter immutable-by-default i64 local slice.
    // No aliasing or implicit conversion; each initializer is evaluated once.
    for (const auto& [program,expected] : std::vector<std::pair<std::string,std::int64_t>>{
      {"fnc main():int{int x=7;return x;}",7},
      {"fnc main():int{const int x=7;return x;}",7},
      {"fnc main():int{const x=7;return x;}",7},
      {"fnc main():int{int x=5;const y=x+3;return y*2;}",16},
      {"fnc main():int{int x=7;const int y=x+2;return y*3;}",27},
      {"fnc add(x:int):int{int z=x+1;return z;} fnc main():int{return add(41);}",42},
      {"fnc add(x:int):int{return x+1;} fnc main():int{int z=add(6);return z*3;}",21},
      {"fnc unused():int{const int x=2;return x;} fnc main():int{return 11;}",11},
      {"fnc main():int{int x=-9223372036854775808;return x;}",
       std::numeric_limits<std::int64_t>::min()}
    }) {
      ensure(interpret(parse_source(program))==expected,
             "immutable type-first source interpreter");
    }
    {
      const auto program=parse_runtime_source(
        "fnc main(n:int):int{int x=n+2;const y=x*3;return y;}");
      const std::array<std::int64_t,1> args{7};
      ensure(interpret(program,args)==27,"runtime-parameter immutable local");
    }
    for (const char* invalid : {
      "fnc main():int{int x=x;return x;}",
      "fnc main():int{const x=x;return x;}",
      "fnc main():int{const x=1;const x=2;return x;}",
      "fnc main():int{const x;return x;}",
      "fnc main():int{const x=1;x=2;return x;}",
      "fnc main():int{int x=y;int y=2;return x;}",
      "fnc main():int{int x=1;int x=2;return x;}",
      "fnc main(x:int):int{int x=2;return x;}",
      "fnc main():int{int x;return x;}",
      "fnc main():int{int x=1; x=2;return x;}",
      "fnc main():int{const int x=1; x=2;return x;}",
      "fnc main():int{int x=1;return x=2;}",
      "fnc main():int{int x=1.5;return x;}",
      "fnc main():int{int x=true;return x;}",
      "fnc main():int{int x=1;return missing;}",
      "fnc unused():int{int x=missing;return x;} fnc main():int{return 1;}",
      "fnc main():int{let x=1;return x;}",
      "fnc main():int{bool x=true;return x;}",
      "fnc main():int{int x=1;int y=2;return x+y; return 0;}"
    }) {
      reject([&] { (void)parse_source(invalid); });
    }
    reject_diagnostic([] {
      parse_source("fnc main():int{int x=1;int x=2;return x;}");
    }, "invalid or duplicate immutable local");
    reject_diagnostic([] {
      parse_source("fnc main():int{int x=x;return x;}");
    }, "expected integer at byte");
    reject_diagnostic([] {
      parse_source("fnc main():int{int x=1; x=2;return x;}");
    }, "immutable local reassignment is forbidden");
    reject_diagnostic([] {
      (void)interpret(parse_source(
        "fnc main():int{int x=9223372036854775807;return x+1;}"));
    }, "i64 add overflow");
    // The configured local budget must reject an extra binding before IR.
    {
      std::string maximum="fnc main():int{";
      for (int i=0;i<32;++i)
        maximum+="int v"+std::to_string(i)+"="+std::to_string(i)+";";
      maximum+="return v31;}";
      ensure(interpret(parse_source(maximum))==31,
             "32 immutable locals are admitted");
      reject_diagnostic([&] {
        std::string excess="fnc main():int{";
        for (int i=0;i<33;++i)
          excess+="const v"+std::to_string(i)+"="+std::to_string(i)+";";
        excess+="return 1;}";
        (void)parse_source(excess);
      }, "immutable local budget exceeded");
    }
    reject_diagnostic([] {
      (void)interpret(parse_source(
        "fnc main():int{const unused=9223372036854775807+1;return 42;}"));
    }, "i64 add overflow");
    reject_diagnostic([] {
      (void)parse_source("fnc main():int{const x=unknown(1);return 42;}");
    }, "unknown function");
    // Independent native-call parser, interpreter and native stack-slot ABI:
    // all three source forms must be positive checked-i64 operations.
    for(const auto* positive : {
      "fnc main():int{int x=1;return x;}",
      "fnc main():int{const int x=1;return x;}",
      "fnc main():int{const x=1;return x;}"
    }) ensure(calls::interpret(calls::parse(positive),{})==1,
              "native-call immutable local admission");
    {
      auto bound=calls::parse(
        "fnc twice(n:int):int{const v=n+1;return v*2;}"
        "fnc main(n:int):int{int result=twice(n);const answer=result+1;return answer;}");
      for(const auto& [input,expected] :
          std::vector<std::pair<std::int64_t,std::int64_t>>{
            {0,3},{7,17},{20,43},{-2,-1}}) {
        ensure(calls::interpret(bound,std::array<std::int64_t,1>{input})==expected,
               "native-call helper-local value parity");
      }
      ensure(calls::emit_asm(bound,Arch::X86_64).find("  call .Lores_fn_")!=
             std::string::npos,"native-call local linked through real call");
      auto lazy=calls::parse(
        "fnc main(n:int):int{const x=n+1;return if x>0 then x else -x;}");
      ensure(calls::interpret(lazy,std::array<std::int64_t,1>{-3})==2,
             "native-call locals inside lazy conditional");
      ensure(calls::interpret(lazy,std::array<std::int64_t,1>{5})==6,
             "native-call locals in positive branch");
      auto invalid_scope=calls::parse("fnc main():int{int x=1;return x;}");
      for(auto& node : invalid_scope.functions[0].nodes)
        if(node.kind==calls::Kind::Local)node.name="missing";
      reject_diagnostic([&]{calls::verify(invalid_scope);},
                        "callable local out of scope");
      auto invalid_binding=calls::parse("fnc main():int{int x=1;return x;}");
      for(auto& node : invalid_binding.functions[0].nodes)
        if(node.kind==calls::Kind::Let)node.name="return";
      reject_diagnostic([&]{calls::verify(invalid_binding);},
                        "invalid callable local name");
    }
    for(const auto* invalid : {
      "fnc main():int{int x=x;return x;}",
      "fnc main():int{const x=x;return x;}",
      "fnc main():int{int x=y;int y=2;return x;}",
      "fnc main():int{int x=1;int x=2;return x;}",
      "fnc main(n:int):int{int n=1;return n;}",
      "fnc main():int{int x=1;x=2;return x;}"
    })reject([&] { (void)calls::parse(invalid); });
    reject_diagnostic([] { calls::parse(
      "fnc main():int{int x=1;x=2;return x;}"); },
      "immutable local reassignment is forbidden");
    reject_diagnostic([] { (void)calls::interpret(calls::parse(
      "fnc main():int{const unused=9223372036854775807+1;return 42;}"),{}); },
      "i64 add overflow");
    {
      std::string maximum="fnc main():int{";
      for(int i=0;i<32;++i)
        maximum+="int v"+std::to_string(i)+"="+std::to_string(i)+";";
      maximum+="return v31;}";
      ensure(calls::interpret(calls::parse(maximum),{})==31,
             "native-call maximum 32 immutable locals");
      std::string exceeded="fnc main():int{";
      for(int i=0;i<33;++i)
        exceeded+="int v"+std::to_string(i)+"="+std::to_string(i)+";";
      exceeded+="return v32;}";
      reject_diagnostic([&]{calls::parse(exceeded);},"immutable local budget exceeded");
    }
    // The four byte-pinned shared negative cases exercise the *actual*
    // Forge source parser, verified i64 IR and interpreter. An exception
    // from either admission or execution is allowed; wrapping is not.
    constexpr std::array<std::string_view,4> overflow_diagnostics{
      "i64 add overflow", "i64 sub overflow",
      "i64 mul overflow", "i64 sub overflow"
    };
    for (std::size_t i=0; i<kOverflowNumericalCases.size(); ++i) {
      const std::string expression{kOverflowNumericalCases[i]};
      // No unrelated source-parse/IR error may count as a numeric pass.
      reject_diagnostic([&] { (void)interpret(source(expression)); },
                        overflow_diagnostics[i]);
    }
    // The bootstrap intentionally has NO BigInt, Decimal or binary64 lowering.
    // Fail closed rather than treating those spellings as signed i64.
    reject([] { source("1.25"); });
    reject([] { source("2.0i"); });
    reject([] { parse_source("fnc main():bigint{return 1;}"); });
    reject([] { parse_source("fnc main():decimal{return 1;}"); });
    // Import graph linking is not available in the Forge scalar bootstrap.
    // Reject with the *right* diagnostic before IR or assembly emission.
    for (const auto& path : {"+/src/helper", "+/../outside", "+/src/../helper",
                             "+//src/helper", "+/src\\\\helper", "+src/helper",
                             "@/src/helper", "pkg:math/src/helper", "pkg:missing/helper",
                             "+/nested/lib/child", "+/nested/parent-alias",
                             "+/dangling/lib/child", "./helper", "../helper",
                             "common/helper", ".hidden/helper"}) {
      reject_diagnostic([&] {
        parse_source(std::string("import fnc helper from ") +
                     static_cast<char>(34) + path + static_cast<char>(34) +
                     "; fnc main():int{return 42;}");
      }, "source imports require verified module graph lowering");
    }
    // A comment ends at CR as well as LF. Otherwise a CR-only import line
    // is silently skipped and the scalar artifact could look successful.
    for (const std::string ending : {"\r", "\r\n", "\n"}) {
      reject_diagnostic([&] {
        parse_source("fnc main():int{return 42;} // comment"+ending+
                     "import fnc helper from \"./other\";");
      }, "source imports require verified module graph lowering");
    }
    ensure(interpret(parse_source("fnc main():int{return 42;} // import ignored\r"))==42,
           "CR-only trailing comment must stay a comment");
    ensure(interpret(parse_source("fnc main():int{return 42;} // import ignored"))==42,
           "import in a comment does not trigger module admission");
    // Nominal enums and same-arity virtual method dispatch are not part of
    // this scalar frontend. Deny the declaration with a specific diagnostic,
    // rather than claiming parity from a synthetic policy helper.
    for (const auto& invalid : {
        "define enum Color as\n  Red\nend\nfnc main():int{return 1;}",
        "define enum Color as\n  Red\nend\ndefine enum Mode as\n  Fast\nend\n"
            "fnc pick(Color x):int{return 1;} fnc pick(Mode x):int{return 2;} "
            "fnc main():int{return 0;}"
    }) {
      reject_diagnostic([&] { parse_source(invalid); },
                        "nominal enum overloads require compiler-bound dispatch");
    }
    ensure(interpret(parse_source("fnc main():int{return 1;} // enum comment"))==1,
           "enum spelling in a comment is not a declaration");
    ensure(interpret(parse_source(
               "fnc main():int{/* enum Color /* enum Mode */ nested */return 1;}"))==1,
           "nested enum comments do not become declarations");
    ensure(interpret(parse_source(
               "fnc main():int{return 1;} /* enum Color */"))==1,
           "trailing block-comment enum is not executable");
    reject_diagnostic([] {
      parse_source("fnc main():int{return 1;} /* enum Color");
    }, "unterminated block comment");
    {
      std::string excessive="fnc main():int{return 1;} ";
      for (int i=0; i<129; ++i) excessive+="/*";
      for (int i=0; i<129; ++i) excessive+="*/";
      reject_diagnostic([&] { parse_source(excessive); },
                        "block comment nesting too deep");
    }
    // No private-abstract class can be lowered by the scalar bootstrap.
    // A specific typed-vtable diagnostic is required (not an accidental
    // generic parser exception that could mask partial code publication).
    reject_diagnostic([&] {
      parse_source("define abstract class Framework as "
                   "private abstract receive(int x): int; end");
    }, "private abstract hooks require typed native vtables");

    // The scalar bootstrap has no class vtable, receiver loan, or closure
    // representation. New language constructs must fail closed until their
    // actual native codegen and ownership checking are present.
    for (const char* program : {
      "define class Foo as fnc read(self, int x): int { return x; } end",
      "define actor Foo as receive(self, int x): void { return; } end",
      "define actor Foo as on_start(self): void { return; } end",
      "define actor Foo as receive(ActorMail<int> mail): void { self.receive(mail); } end",
      "define class Foo as private abstract receive(int x): int; end",
      "fnc main():int{return lex[x] || -> x;}",
      "fnc main():int{return nlex[x] || -> x;}",
      "fnc main():int{return nlex || -> 1;}",
      "fnc main():int{return lex[self] || -> 1;}",
      "define class Foo as fnc read(mut self, int x): int { return x; } end",
      "define class Foo as fnc read(self Foo)(int x): int { return x; } end",
      "fnc main():int{return lex[x,x] || -> x;}",
      "fnc main():int{return lex[missing] || -> x;}",
      "fnc main():int{return lex[] || -> x;}",
      "fnc main():int{return nlex[self] || -> x;}",
      "fnc main():int{return nlex[] || -> x;}",
      "fnc main():int{return lex[x] |x| -> x;}",
      "fnc main():int{return |y| -> x;}",
      "fnc main():int{return lex[x] || -> nlex || -> x;}",
      "fnc main():int{return nlex[self] critical |state| -> 1;}",
    }) {
      // A catch-all parser exception is not proof of fail-closed feature
      // admission. Check that the unsupported feature itself was diagnosed.
      const std::string_view text(program);
      const bool method = text.find("define class") != std::string_view::npos ||
                          text.find("define actor") != std::string_view::npos;
      reject_diagnostic([&] { parse_source(program); }, method
                        ? "class/actor methods require typed native lowering"
                        : "lexical captures require checked closure lowering");
    }

    for (const auto& opaque : {"any", "unknown", "Any", "Unknown"}) {
      reject_diagnostic([&] {
        parse_source(std::string("fnc main():") + opaque + "{return 1;}");
      }, "tagged opaque-value ABI");
    }
    reject_diagnostic([] {
      parse_source("fnc main():int{return 1 ? 2 : 3;}");
    }, "ternary expression requires bool typing and CFG branch-merge lowering");
    ensure(interpret(parse_source("fnc main():int{return 42;} // any unknown ?"))==42,
           "opaque-top and ternary tokens in comments remain invisible");
    // Named-call and tagged pipeline lowering are still unavailable in
    // Forge's i64 bootstrap. Verify explicit rejection before codegen.
    for (const auto& expression : {
        "1 |> twice", "(1 |> twice)", "1+2 |> plus(3)",
        "1 |> Option.map(inc)", "1 |> Result.map_err(annotate)",
        "1 |> Option /*comment*/ . map(inc)",
        "1 |> plus(2 |> Result.map_err(f))"
    }) {
      reject_diagnostic([&] { source(expression); },
                        "value pipeline requires named callable lowering");
    }
    ensure(interpret(parse_source("fnc main():int{return 1;} // |> ignored"))==1,
           "value pipeline syntax inside comments must not be tokenized");
    reject_diagnostic([] {
      parse_source("fnc take(tuple (int,int) value):int{return 1;} fnc main():int{return 1;}");
    },"tuple-v2 parameter requires typed ownership/ABI lowering");
    reject_diagnostic([] {
      parse_source("fnc take(structural (int,int) value):int{return 1;} fnc main():int{return 1;}");
    },"tuple-v2 parameter requires typed ownership/ABI lowering");
    reject_diagnostic([] {
      parse_source("fnc take((int,int) value):int{return 1;} fnc main():int{return 1;}");
    },"tuple-v2 parameter requires typed ownership/ABI lowering");
    reject_diagnostic([] {
      parse_source("fnc take():tuple (int,int){return 1;} fnc main():int{return 1;}");
    },"tuple-v2 return requires typed ABI lowering");
    reject_diagnostic([] {
      parse_source("fnc pair():int{return 1;} fnc main():int{return pair(...pair);}");
    },"tuple-v2 explicit spread requires single-evaluation checked lowering");

    reject_diagnostic([] { parse_source("fnc main():int{return rt ref value;}"); },
                      "pointer-style runtime operation is not valid Oreslang source");
    reject_diagnostic([] { parse_source("fnc main():int{return rt deref value;}"); },
                      "pointer-style runtime operation is not valid Oreslang source");
    reject_diagnostic([] { parse_source("fnc main():int{return rt borrow mut value;}"); },
                      "legacy 'rt borrow mut' is not valid Oreslang");
    reject_diagnostic([] { parse_source("fnc main():int{return rt mut borrow value;}"); },
                      "runtime ownership operation requires typed lowering in Forge");
    reject([] { parse_source("fnc main():int{return &value;}"); });
    reject([] { parse_source("fnc main():int{return &mut value;}"); });
    reject([] { parse_source("fnc main():int{return *value;}"); });
    reject([] { parse_source("fnc main():Pointer<int>{return value;}"); });

    ensure(interpret(source("2+3*4"))==14,"precedence");

    // CPU/GPU Task v2 is reserved fail-closed until the Forge scheduler/device
    // graph is implemented. 'task' remains contextual: only task { ... } is special.
    reject_diagnostic([] {
      parse_source("pub fnc main():int{return cpu { 42 };}");
    }, "execution region requires verified CPU/GPU Task lowering");
    reject_diagnostic([] {
      parse_source("pub fnc main():int{return cpu task { 42 };}");
    }, "execution region requires verified CPU/GPU Task lowering");
    reject_diagnostic([] {
      parse_source("pub fnc main():int{return gpu task { 42 };}");
    }, "execution region requires verified CPU/GPU Task lowering");
    reject_diagnostic([] {
      parse_source("pub fnc main():int{return task { 42 };}");
    }, "execution task requires scheduler-aware Future/continuation lowering");
    reject_diagnostic([] {
      parse_source("pub fnc main():int{return nb { 42 };}");
    }, "execution region requires verified CPU/GPU Task lowering");

    // Both source frontends must validate bytes before ignoring comments.
    // A lexical skip is not permission to admit malformed source encodings.
    const std::string callable_comment="fnc main():int{return 1;} // ";
    for (const std::string bad : {
        std::string("\xc0\xaf",2), std::string("\xed\xa0\x80",3),
        std::string("\xe2\x82",2), std::string("\xf4\x90\x80\x80",4),
        std::string("\x80",1)}) {
      reject_diagnostic([&] { calls::parse(callable_comment+bad); },
                        "not valid UTF-8");
    }
    reject_diagnostic([&] {
      calls::parse(callable_comment+std::string(1,'\0'));
    }, "source contains NUL byte");
    ensure(calls::parse(callable_comment+"caf\xc3\xa9").functions.size()==1,
           "valid UTF-8 callable comment");
    // A CR-only line terminator must not conceal a following declaration.
    // The callable parser previously skipped until LF and silently discarded
    // the broken second declaration, publishing a misleading artifact.
    for (const std::string ending : {"\r","\r\n","\n"}) {
      reject_diagnostic([&] {
        calls::parse("fnc main():int{return 42;} // discarded"+ending+
                     "fnc bad():int{return unresolved;}");
      }, "unknown parameter");
      ensure(calls::interpret(calls::parse(
        "fnc main():int{return 42;} // ignore"+ending+
        "fnc spare():int{return 3;}"),{})==42,
        "callable line comments cannot swallow another definition");
    }
    // Opt-in callable program has its own fully verified representation.
    // Unlike the scalar profile, these helpers compile to real native calls.
    auto fact=calls::parse(
      "fnc fact(n:int):int{return if n<=1 then 1 else n*fact(n-1);}"
      " fnc main(n:int):int{return fact(n);}");
    ensure(calls::interpret(fact,std::array<std::int64_t,1>{5})==120,
           "bounded direct recursion and lazy branch");
    ensure(calls::interpret(fact,std::array<std::int64_t,1>{10})==3628800,
           "recursive checked i64 return");
    ensure(calls::emit_asm(fact,Arch::X86_64).find("  call .Lores_fn_")!=std::string::npos,
           "real native call emitted");
    auto mutually_recursive=calls::parse(
      "fnc even(n:int):int{return if n==0 then 1 else odd(n-1);}"
      " fnc odd(n:int):int{return if n==0 then 0 else even(n-1);}"
      " fnc main(n:int):int{return even(n);}");
    ensure(calls::interpret(mutually_recursive,std::array<std::int64_t,1>{7})==0,
           "mutual recursion supported");
    ensure(calls::interpret(mutually_recursive,std::array<std::int64_t,1>{8})==1,
           "mutual recursion parity");
    reject_diagnostic([&] { calls::interpret(fact,std::array<std::int64_t,1>{21}); },
                      "i64 mul overflow");
    reject_diagnostic([&] { calls::emit_asm(fact,Arch::ARM64); },
                      "currently supports x86_64 only");
    reject_diagnostic([] { calls::parse("fnc main():int{return other();}"); },
                      "unknown function");
    reject_diagnostic([] { calls::parse(
      "fnc other(x:int):int{return x;}"
      " fnc main():int{return other();}"); },"function arity mismatch");
    auto corrupt=calls::parse("fnc main(x:int):int{return x+1;}");
    corrupt.functions[0].nodes.back().children[0]=999;
    reject_diagnostic([&] { calls::verify(corrupt); },
                      "callable use before definition");

    auto reused=calls::parse("fnc main():int{return 2+3;}");
    reused.functions[0].nodes.back().children[1]=
      reused.functions[0].nodes.back().children[0];
    reject_diagnostic([&] { calls::verify(reused); },
                      "callable AST shared node");
    auto unreachable=calls::parse("fnc main():int{return 2+3;}");
    unreachable.functions[0].result=0;
    reject_diagnostic([&] { calls::verify(unreachable); },
                      "invalid callable return/root");
    auto invalid_kind=calls::parse("fnc main():int{return 2+3;}");
    invalid_kind.functions[0].nodes[0].kind=static_cast<calls::Kind>(99);
    reject_diagnostic([&] { calls::verify(invalid_kind); },
                      "unknown callable opcode");

    auto endless=calls::parse(
      "fnc forever(n:int):int{return forever(n);}"
      " fnc main(n:int):int{return forever(n);}");
    reject_diagnostic([&] {
      calls::interpret(endless,std::array<std::int64_t,1>{1});
    },"call depth exceeded");


    // Separate runtime-entry profile lowers parameter IR rather than folding
    // the entire program to a constant. Helpers are STILL statically inlined.
    auto runtime=parse_runtime_source(
      "fnc helper(x:int,y:int):int{return x*3-y;} "
      "fnc main(a:int,b:int):int{return helper(a,b)+b*2;}");
    ensure(runtime.parameter_count==2, "runtime param count");
    ensure(interpret(runtime,std::array<std::int64_t,2>{7,13})==34,
           "runtime parameters change checked result");
    ensure(interpret(runtime,std::array<std::int64_t,2>{-7,13})==-8,
           "negative runtime params");
    ensure(dump_ir(runtime).find("param.i64 0")!=std::string::npos,
           "parameter instruction not constant-folded");
    reject_diagnostic([&] { interpret(runtime); },"runtime argument count mismatch");
    reject_diagnostic([&] { interpret(runtime,std::array<std::int64_t,1>{1}); },
                      "runtime argument count mismatch");
    reject_diagnostic([&] { interpret(runtime,std::array<std::int64_t,2>{
        std::numeric_limits<std::int64_t>::max(),2}); }, "i64 mul overflow");
    const auto x86_native=emit_runtime_asm(runtime,Arch::X86_64);
    ensure(x86_native.find("imulq ")!=std::string::npos &&
           x86_native.find("jo .Loresforge_overflow")!=std::string::npos &&
           x86_native.find("%rdi")!=std::string::npos,
           "x86 checked runtime operations and ABI");
    const auto arm_native=emit_runtime_asm(runtime,Arch::ARM64);
    ensure(arm_native.find("smulh x12")!=std::string::npos &&
           arm_native.find("b.vs .Loresforge_overflow")!=std::string::npos &&
           arm_native.find("brk #0")!=std::string::npos,
           "arm checked runtime operations and ABI");
    reject_diagnostic([] {
      parse_runtime_source(
        "fnc main(a:int,b:int,c:int,d:int,e:int):int{return a;}");
    }, "runtime entry parameter budget exceeded");
    reject_diagnostic([] { parse_source("fnc main(a:int):int{return a;}"); },
                      "main must have zero parameters");
    Program broken_params{{{Op::Parameter,1},{Op::Return,0,0,-1}},1};
    reject_diagnostic([&] { verify(broken_params); },
                      "invalid runtime parameter index");
    broken_params.instructions[0].value=0;
    ensure(interpret(broken_params,std::array<std::int64_t,1>{99})==99,
           "verified runtime parameter");
    reject_diagnostic([&] { emit_asm(broken_params,Arch::X86_64); },
                      "runtime argument count mismatch");
    Program runtime_division{{{Op::Constant,8},{Op::Constant,2},
                              {Op::Div,0,0,1},{Op::Return,0,2,-1}}};
    reject_diagnostic([&] { emit_runtime_asm(runtime_division,Arch::X86_64); },
                      "runtime i64 division lowering not supported");


    // Stage-0 typed-pure-function inliner. These check semantics and reject
    // invalid whole-module definitions; they are NOT native stack-frame calls.
    ensure(interpret(parse_source(
      "fnc main():int{return mul_add(6,7)+mul_add(2,3);} "
      "fnc mul_add(a:int,b:int):int{return a*b;}"))==48,
      "typed two-parameter calls and repeated inlining");
    ensure(interpret(parse_source(
      "fnc twice(v:int):int{return v*2;} "
      "define pub fnc main()->int{return twice(twice(7));}"))==28,
      "nested call binding and arrow type");
    ensure(interpret(parse_source(
      "fnc main():int{return forward(-3);} "
      "fnc forward(v:int):int{return v*v;}"))==9,
      "forward definition and negative argument");
    ensure(interpret(parse_source(
      "fnc unused(x:int):int{return x+1;} "
      "fnc main():int{return 17;}"))==17,
      "unused valid function");
    reject_diagnostic([] { parse_source(
      "fnc main():int{return self(1);} "
      "fnc self(x:int):int{return self(x);}"); },
      "recursive calls not supported");
    reject_diagnostic([] { parse_source(
      "fnc a():int{return b();} fnc b():int{return a();} "
      "fnc main():int{return 0;}"); },
      "recursive calls not supported");
    reject_diagnostic([] { parse_source(
      "fnc unused():int{return missing();} fnc main():int{return 0;}"); },
      "unknown function");
    reject_diagnostic([] { parse_source(
      "fnc unused():int{return ok(1,2);} fnc ok(a:int):int{return a;} "
      "fnc main():int{return 0;}"); },
      "arity mismatch");
    reject_diagnostic([] { parse_source(
      "fnc main():int{return 1;} fnc main():int{return 2;}"); },
      "duplicate function");
    reject_diagnostic([] { parse_source(
      "fnc bad(x:int,x:int):int{return x;} fnc main():int{return 1;}"); },
      "duplicate parameter");
    reject_diagnostic([] { parse_source(
      "fnc main(x:int):int{return x;}"); },
      "main must have zero parameters");
    reject_diagnostic([] { parse_source(
      "fnc main():int{return missing(1);}"); },
      "unknown function");

    // DAG validation must be memoized and inlining itself must respect the
    // existing instruction budget (never exponential unchecked allocation).
    std::string expanding="fnc f0():int{return 1;}";
    for (int i=1; i<19; ++i) {
      const std::string prev="f"+std::to_string(i-1)+"()";
      expanding+="fnc f"+std::to_string(i)+"():int{return "+
                 prev+"+"+prev+";}";
    }
    expanding+="fnc main():int{return f18();}";
    reject_diagnostic([&] { parse_source(expanding); }, "too many instructions");
    // Even a linear, acyclic call chain must stop at the configured depth.
    std::string deep;
    for (int i=0; i<66; ++i)
      deep+="fnc f"+std::to_string(i)+"():int{return "+
            (i==65?std::string("1"):("f"+std::to_string(i+1)+"()"))+";}";
    deep+="fnc main():int{return f0();}";
    reject_diagnostic([&] { parse_source(deep); }, "function call depth exceeded");
    reject_diagnostic([] { parse_source(
      "fnc bad(x:int):int{return missing_param;} fnc main():int{return 1;}"); },
      "expected integer at byte");

    reject_diagnostic([] { interpret(parse_source(
      "fnc main():int{return identity(9223372036854775807)+1;} "
      "fnc identity(x:int):int{return x;}")); }, "i64 add overflow");

    ensure(interpret(source("(2+3)*4"))==20,"parentheses");
    // Cross-backend Wave 2 scalar fixture (Git blob a8165323...).
    // This is a genuine evaluated Oreslang source result, not a parser-only gate.
    const std::string arithmetic76=R"ores(pub fnc main(): int {
  return 100 - (3 + 2) - 19;
}
)ores";
    ensure(interpret(parse_source(arithmetic76))==76,
           "shared arithmetic-76 source execution");
    for (const auto& expression : {"3/2", "4/2", "5/0", "-9223372036854775808/-1"})
      reject_diagnostic([&] { source(expression); }, "float result lowering");
    for (const auto& expression : {"0.125", "1e10", "1E-10", "123.456d", "123d"})
      reject_diagnostic([&] { source(expression); }, "typed numeric lowering");
    // Internal checked-i64 IR division is distinct from the source / operator.
    Program internal_division{{{Op::Constant,-3},{Op::Constant,2},
                              {Op::Div,0,0,1},{Op::Return,0,2,-1}}};
    ensure(interpret(internal_division)==-1,"internal checked division");
    internal_division.instructions[1].value=0;
    reject_diagnostic([&] { interpret(internal_division); }, "division by zero");
    internal_division.instructions[0].value=std::numeric_limits<std::int64_t>::min();
    internal_division.instructions[1].value=-1;
    reject_diagnostic([&] { interpret(internal_division); }, "division overflow");
    ensure(interpret(source("-(3+2)"))==-5,"unary");
    ensure(interpret(parse_source("pub define fnc main():int { return 9; }"))==9,"pub define");
    ensure(interpret(parse_source("define pub fnc main() -> int { return 2+3*4; }"))==14,
           "canonical formatter arrow");
    reject([] { parse_source("fnc main() - > int { return 1; }"); });
    reject([] { parse_source("fnc main() ->> int { return 1; }"); });
    reject([] { source("9223372036854775808"); });
    ensure(interpret(source("-9223372036854775808"))==
               std::numeric_limits<std::int64_t>::min(),"i64 minimum");
    reject([] { interpret(source("--9223372036854775808")); });
    reject([] { interpret(source("-9223372036854775808/-1")); });
    reject([] { parse_source(std::string(1024 * 1024 + 1, ' ')); });
    // These are complete *valid* programs with invalid bytes inside comments.
    // A generic assertThrows would pass for an unrelated lexical error; require
    // the encoding-specific diagnostic even where lexing would ignore comments.
    const std::string comment="fnc main():int {return 1;} // ";
    reject_diagnostic([&] { parse_source(comment+
        static_cast<char>(0xc0)+static_cast<char>(0xaf)); },"not valid UTF-8");
    reject_diagnostic([&] { parse_source(comment+
        static_cast<char>(0xed)+static_cast<char>(0xa0)+static_cast<char>(0x80)); },
        "not valid UTF-8");
    reject_diagnostic([&] { parse_source(comment+
        static_cast<char>(0xe2)+static_cast<char>(0x82)); },"not valid UTF-8");
    reject_diagnostic([&] { parse_source(comment+
        static_cast<char>(0xf4)+static_cast<char>(0x90)+
        static_cast<char>(0x80)+static_cast<char>(0x80)); },"not valid UTF-8");
    reject_diagnostic([&] { parse_source(comment+
        static_cast<char>(0x80)); },"not valid UTF-8");
    reject_diagnostic([&] { std::string text=comment; text.push_back('\0');
        parse_source(text); },"source contains NUL byte");
    ensure(interpret(parse_source("fnc main():int {return 3;} // caf\xc3\xa9"))==3,
           "valid UTF-8 comment");

    const std::string block_comment="fnc main():int{return 1;} /* ";
    reject_diagnostic([&] { parse_source(block_comment+
        static_cast<char>(0xc0)+static_cast<char>(0xaf)+" */"); },
        "not valid UTF-8");
    reject_diagnostic([&] { std::string text=block_comment;
        text.push_back('\0'); text+=" */"; parse_source(text); },
        "source contains NUL byte");
    ensure(interpret(parse_source(
               "fnc main():int{return 3;} /* caf\xc3\xa9 enum */"))==3,
           "valid UTF-8 in ignored block comment");
    // UTF-8 source is accepted for comments, not grammar tokens.
    // This must be stable under every process locale.
    reject_diagnostic([] {
      parse_source("fnc main():int {return caf\xc3\xa9;}");
    }, "invalid character");
    reject_diagnostic([] {
      parse_source("fnc main():int {return 1\xc2\xa0;}");
    }, "invalid character");
    reject_diagnostic([] {
      parse_source("fnc main():int {return \xce\xbb;}");
    }, "invalid character");
    ensure(interpret(parse_source("fnc main():int {return 42;} // \xf0\x9f\x8c\x9f"))==42,
           "supplementary Unicode comment is lexically inert");


    reject([] { source(std::string(200, '(') + "1" + std::string(200, ')')); });
    reject([] { source(std::string(200, '-') + "1"); });
    reject([] {
      std::string expr="1";
      for (int i=0; i<50000; ++i) expr += "+1";
      source(expr);
    });
    reject([] {
      Program too_large;
      too_large.instructions.resize(100001, {Op::Constant,0});
      verify(too_large);
    });
    reject([] { interpret(source("9223372036854775807 + 1")); });
    reject([] { interpret(source("3/0")); });
    // Structural declarations, argument shape matching and match-over are not
    // implemented by the arithmetic bootstrap. Never silently accept them.
    // PR #18: preserve these less common surfaces alongside the newer tuple,
    // shape, numeric and import regressions already on main. A runtime error
    // is not sufficient: unsupported syntax must fail at source admission.
    for (const auto candidate : {
      "structural routine main():int{return 1;}",
      "routine main(structural Shape value):int{return 1;}",
      "fnc main():int{return match 1 over on _ -> {return 1;} end;}",
      "fnc main():int{do match 1 over on structural {x:int} v when true -> {return 1;} on _ -> {return 0;} end}",
      "fnc main():int{do match 1 over on _ -> {return 1;} end}"
    }) {
      reject_diagnostic([&] { (void)parse_source(candidate); }, "expected");
    }
    reject_diagnostic([] {
      (void)parse_source("@structural\nfnc main():int{return 1;}");
    }, "invalid character");
    reject_diagnostic([] {
      parse_source("structural fnc main():int{return 1;}");
    }, "expected");
    reject_diagnostic([] {
      parse_source("fnc main(structural Shape value):int{return 1;}");
    }, "tuple-v2 parameter requires typed ownership/ABI lowering");
    reject_diagnostic([] {
      parse_source("fnc main():int{match 1 over on _ -> {return 1;} end}");
    }, "expected");
    // Task 70: structural and row syntax must fail closed across declaration,
    // parameter, projection and pattern surfaces. The scalar bootstrap has
    // neither row layout witnesses nor borrow/mutable-field capability checks.
    const std::vector<std::string> unsupported_shapes={
      "fnc take(record{foo:int} value):int{return value.foo;}",
      "fnc take(struct{foo:int} value):int{return value.foo;}",
      "fnc take(structural {foo:int} value):int{return value.foo;}",
      "fnc take(structural Shape value):int{return 1;}",
      "fnc take(List<record{foo:int}> value):int{return 1;}",
      "fnc main():int{const x=infer struct{foo:1,bar:2};return 1;}",
      "fnc main():int{const x=struct{foo:int}{foo:1};return 1;}",
      "fnc main():int{match 1 over on structural {foo:int} as x {return 1;} end}",
      "fnc take(record{foo:int,foo:string} value):int{return 1;}"
    };
    for (const auto& input : unsupported_shapes) {
      reject([&] { parse_source(input); });
    }
    ensure(interpret(parse_source(
      "fnc main():int{return 42;} // record{foo:int}, structural Shape"))==42,
      "shape words in comments must not cause false-positive admission");
    // v2 tuple syntax is intentionally not executable on Forge bootstrap.
    reject([] {
      parse_source("fnc take(tuple (int,int,string?) v):int{return 1;}");
    });
    reject([] {
      parse_source("fnc take((int,int,string?) v):int{return 1;}");
    });
    reject([] {
      parse_source("fnc take(structural (int,int,string?) v):int{return 1;}");
    });
    reject([] { parse_source("fnc main():int {return 1;} garbage"); });
    reject([] { parse_source("fnc main():int {return x;}"); });
    reject([] { parse_source("pub pub fnc main():int {return 1;}"); });
    Program broken{{{Op::Add,0,0,0},{Op::Return,0,0,-1}}};
    reject([&] { verify(broken); });
    Program missing{{{Op::Constant,3}}};
    reject([&] { verify(missing); });
    ensure(dump_ir(source("2+3*4")).find("mul.i64.checked")!=std::string::npos,"IR dump");
    ensure(emit_asm(source("2+3*4"),Arch::X86_64).find("movabs $14")!=std::string::npos,"x86_64 asm");
    ensure(emit_asm(source("2+3*4"),Arch::ARM64).find("movz x0, #14")!=std::string::npos,"arm64 asm");
    reject([] { emit_asm(source("1/0"),Arch::X86_64); });
    reject_diagnostic([] { emit_asm(source("1"),static_cast<Arch>(99)); },
                      "unsupported architecture");
    jit::Executor executor({2,3});
    ensure(executor.run(source("1+2"))==3,"interpreter");
    ensure(executor.requested_tier()==jit::Tier::Interpreter,"tier0");
    executor.run(source("1+2"));
    ensure(executor.requested_tier()==jit::Tier::BaselineRequested,"tier1 request");
    executor.run(source("1+2"));
    ensure(executor.requested_tier()==jit::Tier::OptimizedRequested,"tier2 request");
    ensure(executor.effective_tier()==jit::Tier::Interpreter,"fallback honest");
    static_assert(!std::is_constructible_v<runtime::ActorRef,
                  std::uint64_t, std::uint64_t>,
                  "ActorRef must not be constructible from numeric IDs");
    runtime::Mailbox box(9,1,4);
    auto old=box.ref();
    std::array<std::uint8_t,3> bytes={1,2,3};
    box.send(old,bytes);
    bytes[0]=99;
    reject([&] { box.send(old,bytes); });
    ensure(box.queued()==1,"capacity");
    auto received=box.receive();
    ensure(received && received->at(0)==1,"deep copy");
    ensure(!box.receive(),"empty");
    runtime::Mailbox wrong(10,1,4);
    reject([&] { box.send(wrong.ref(),bytes); });
    runtime::Mailbox duplicate(9,1,4);
    reject([&] { box.send(duplicate.ref(),bytes); }); // same ID+generation
    box.close();
    reject([&] { box.send(old,bytes); });
    auto next=box.restart();
    ensure(next.generation>old.generation,"generation");
    ensure(box.queued()==0,"restart discarded all pending envelopes");
    reject([&] { box.send(old,bytes); });
    reject([&] { box.send(duplicate.ref(),bytes); });
    box.send(next,bytes);
    std::vector<std::uint8_t> huge(runtime::kMaxMessageBytes+1);
    reject([&] { box.send(next,huge); });
    box.close();
    ensure(box.queued()==0,"stop discarded pending owned envelopes");
    reject([&] { box.send(next,bytes); });
    auto newest=box.restart();
    reject([&] { box.send(next,bytes); });
    box.send(newest,bytes);
    ensure(box.queued()==1,"fresh capability works after second restart");
    std::cout<<"OresForge bootstrap tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr<<"FAILED: "<<error.what()<<'\n';
    return 1;
  }
}
