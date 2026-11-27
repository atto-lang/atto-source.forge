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
#include <fstream>
#include <iterator>
#include <set>
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
                               std::string(error.what())+" (expected: "+std::string(diagnostic)+")");
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
    // Checked literal aggregate aliases reach the existing scalar IR and
    // native emitter, not a host-language aggregate evaluator.
    const auto named_aggregates=parse_source(
      "pub fnc main():int{"
      "const Record<infer R> r=(a=1,b=true);"
      "const Tuple<infer T> t=(true,2);"
      "const struct<infer S> s={a:3};"
      "const Record<R> r2=(b=false,a=10);"
      "const Tuple<T> t2=(false,12);"
      "const struct<S> s2={a:20};"
      "return r2.a+t2[1]+s2.a;}");
    ensure(interpret(named_aggregates)==42,"named aggregate scalar interpreter");
    ensure(emit_asm(named_aggregates,Arch::X86_64).find("movabs $42")!=std::string::npos,
           "named aggregate x86_64 emission");
    ensure(emit_asm(named_aggregates,Arch::ARM64).find("movz x0, #42")!=std::string::npos,
           "named aggregate arm64 emission");
    ensure(interpret(parse_source("fnc main():int{const Record r=(a=10);"
      "const Tuple t=(true,12);const struct s={a:20};return r.a+t[1]+s.a;}"))==42,
      "anonymous aggregate has no type binding");
    for (const auto* body : {
      "const Record<infer> r=(a=1);return 0;",
      "const Record<R> r=(a=1);return 0;",
      "const Record<infer Map> r=(a=1);return 0;",
      "const Record<infer R> r=(a=1);const Tuple<infer R> t=(1,2);return 0;",
      "const Record<infer R> r=(a=1);const Record<R> s=(a=true);return 0;",
      "const Record<infer R> r=(a=1);const Record<R> s=(a=1,b=2);return 0;",
      "const Record<infer R> r=(a=1,b=2);const Record<R> s=(a=1);return 0;",
      "const Record<infer R> r=(a=1,a=2);return 0;",
      "const Tuple<infer T> t=(1,true);const Tuple<T> s=(true,1);return 0;",
      "const Tuple<infer T> t=(1,true);const Tuple<T> s=(1);return 0;",
      "const struct<infer S> s={a:1};const Record<S> r=(a=1);return 0;",
      "const Record<infer R> r=(a=1);return r;",
      "const Record<infer R> r=(a=true);return r.a;",
      "const Tuple<infer T> t=(1,true);return t[2];",
      "const Record<infer R> r=(a=unknown);return 0;"
    }) reject([&] { (void)parse_source(std::string("fnc main():int{")+body+"}"); });
    reject([&] { (void)parse_source("fnc first():int{const Record<infer R> r=(a=1);return r.a;}"
      "fnc main():int{const Record<R> r=(a=42);return r.a;}"); });
    for (const auto* alias : {"while","select","await","Pointer","ActorMail",
        "ReadLoan","AtomicInt","RwLock","unsafe","persistent"}) {
      const std::string src=std::string("fnc main():int{const Record<infer ")+alias+
          "> r=(a=1);return r.a;}";
      reject_diagnostic([&] { (void)parse_source(src); }, "E_TYPE_NAME");
    }
    reject_diagnostic([&] { (void)parse_source(
      "fnc main():int{const Record<infer R> select=(a=1);return 0;}"); }, "E_TYPE_NAME");
    reject_diagnostic([&] { (void)parse_source(
      "fnc main():int{const struct<infer S> s={await:1};return 0;}"); }, "E_TYPE_NAME");
    ensure(interpret(parse_source("fnc main():int{const Record<infer If> r=(a=1);"
      "const Record<If> s=(a=42);return s.a;}"))==42,"case-sensitive alias");
    const auto changed_aggregates=parse_source("fnc main():int{"
      "const Record<infer R> r=(a=1,b=true);const Record<R> r2=(b=false,a=11);"
      "const Tuple<infer T> t=(true,2);const Tuple<T> t2=(false,14);"
      "const struct<infer S> s={a:3};const struct<S> s2={a:23};"
      "return r2.a+t2[1]+s2.a;}");
    ensure(interpret(changed_aggregates)==48,"distinct aggregate reuse values");
    ensure(emit_asm(changed_aggregates,Arch::X86_64).find("movabs $48")!=std::string::npos,
           "distinct aggregate native emission");
    // Two-word contextual declarations are not executable stage-0 scalar
    // functions; every mode must reject with the correct semantic capability.
    for (std::string_view source_text : {
      "define state container Counter as end",
      "define pub state container Counter as end",
      "pub define state container Counter as end"
    }) {
      reject_diagnostic([&] { (void)parse_source(source_text); },
                        "STATE_CONTAINER_BACKEND_UNSUPPORTED");
      reject_diagnostic([&] { (void)parse_runtime_source(source_text); },
                        "STATE_CONTAINER_BACKEND_UNSUPPORTED");
    }
    for (std::string_view source_text : {
      "define virtual actor Cart as end",
      "define pub virtual actor Cart as end",
      "pub define virtual actor Cart as end"
    }) {
      reject_diagnostic([&] { (void)parse_source(source_text); },
                        "VIRTUAL_ACTOR_BACKEND_UNSUPPORTED");
      reject_diagnostic([&] { (void)parse_runtime_source(source_text); },
                        "VIRTUAL_ACTOR_BACKEND_UNSUPPORTED");
    }
    // Guard is reserved across AttoLang even where Forge stage-0 cannot
    // compile its checked early-exit CFG. Rejection must be precise, and
    // must occur before verified IR or an executable is emitted.
    for (std::string_view guard_source : {
        "fnc main():int{guard true{return 1;}return 42;}",
        "fnc main():int{guard 1>0{return 1;}return 42;}",
        "fnc guard():int{return 42;}",
        "fnc helper(guard:int):int{return 42;} fnc main():int{return 42;}"
    }) {
      reject_diagnostic([&] { (void)parse_source(guard_source); },
                        "GUARD_BACKEND_UNSUPPORTED");
      reject_diagnostic([&] { (void)parse_runtime_source(guard_source); },
                        "GUARD_BACKEND_UNSUPPORTED");
    }
    ensure(interpret(parse_source(
        "fnc main():int{return 42;} // guard is not a token"))==42,
        "guard spelling in comment must remain ignored");
    ensure(interpret(parse_source(
      "fnc main():int{return 42;} // define virtual actor Spoof"))==42,
      "declaration head inside a comment is not executable source");
    // Shared language admission floor: both Forge source frontends must reject
    // semantic attributes, while ordinary annotation-looking comments remain legal.
    std::ifstream annotation_manifest("annotation-admission-v1.tsv");
    ensure(static_cast<bool>(annotation_manifest), "missing annotation admission corpus");
    std::set<std::string> annotation_ids;
    std::string annotation_row;
    while (std::getline(annotation_manifest, annotation_row)) {
      if (annotation_row.empty() || annotation_row.front()=='#') continue;
      const auto first=annotation_row.find('\t');
      const auto second=first==std::string::npos
        ? std::string::npos : annotation_row.find('\t',first+1);
      if (first==std::string::npos || second==std::string::npos ||
          annotation_row.find('\t',second+1)!=std::string::npos)
        throw std::runtime_error("malformed annotation admission TSV row");
      const auto kind=annotation_row.substr(0,first);
      const auto id=annotation_row.substr(first+1,second-first-1);
      const auto encoded=annotation_row.substr(second+1);
      if (id.empty() || encoded.empty() || !annotation_ids.insert(id).second)
        throw std::runtime_error("empty/duplicate annotation admission ID");
      std::string program;
      for (std::size_t i=0; i<encoded.size(); ++i) {
        if (encoded[i]=='\\' && i+1<encoded.size() && encoded[i+1]=='n') {
          program.push_back('\n'); ++i;
        } else {
          program.push_back(encoded[i]);
        }
      }
      if (kind=="shared-deny" || kind=="bootstrap-deny") {
        reject_diagnostic([&] { (void)parse_source(program); }, "annotations require");
        reject_diagnostic([&] { (void)calls::parse(program); }, "annotations require");
      } else if (kind=="shared-allow") {
        ensure(interpret(parse_source(program))==42, "Forge annotation scalar positive");
        (void)calls::parse(program);
      } else {
        throw std::runtime_error("unknown annotation admission case kind");
      }
    }
    ensure(annotation_ids.size()>=12, "incomplete annotation admission corpus");
    // Cross-backend i64 positive controls run against the real Forge parser,
    // verified IR and checked interpreter. These are not Python-only data tests.
    for (const auto& [expression, expected] : kPortableNumericalCases) {
      ensure(interpret(source(expression))==expected,"numeric parity i64 source");
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
    // Explicit inverted layouts are not scalar IR and must never be
    // silently lowered as ordinary structs/classes or plain Array<T>.
    for (const auto& invalid : {
      "define inverted struct Point as int x; int y; end",
      "define pub inverted struct Point as int x; end",
      "pub define inverted class Point as int x; end",
      "define /* harmless */ inverted /* still a declaration */ struct Point as int x; end"
    }) {
      reject_diagnostic([&] { (void)parse_source(invalid); },
                        "inverted struct/class requires one-level typed SoA");
    }
    reject_diagnostic([&] {
      (void)parse_source(
        "fnc main():int{return InvertedArray<Point>.with_capacity(1); }");
    }, "inverted collection requires typed owned SoA columns");
    ensure(interpret(parse_source(
              "fnc main():int{return 42;} // define inverted struct ignored"))==42,
           "inverted declaration text inside comment must be inert");
    ensure(interpret(parse_source(
              "fnc main():int{/* define inverted struct Nested */return 42;}"))==42,
           "nested comment mention must not enable inverted layouts");

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
    // Both frontends deny ModelGraph before IR, with contextual syntax.
    for (const auto& declaration : {
        "define model PriceEstimate() as AddTax(10) end",
        "define model x as Identity() end",
        "define pub model PriceEstimate() as AddTax(10) end",
        "pub define model PriceEstimate() as AddTax(10) end",
        "define /* comment */ model PriceEstimate() as AddTax(10) end"
    }) {
      reject_diagnostic([&] { (void)parse_source(declaration); },
                        "MODEL_LOWERING_UNSUPPORTED");
      reject_diagnostic([&] { (void)parse_runtime_source(declaration); },
                        "MODEL_LOWERING_UNSUPPORTED");
    }
    ensure(interpret(parse_source(
        "fnc model():int{return 41;} fnc main():int{return model()+1;}"))==42,
        "model remains a contextual function name");
    ensure(interpret(parse_source(
        "/* define model Fake as Op() end */ fnc main():int{return 42;}"))==42,
        "model declarations in comments are inert");
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

    // Generic serial actors are distinct from source HTTP actors and must
    // fail with their OWN compiler-owned unsupported capability marker.
    for (const char* program : {
      "define serial actor Worker as end",
      "pub define serial actor Worker as end"
    }) {
      reject_diagnostic([&] { (void)parse_source(program); },
                        "SERIAL_ACTOR_BACKEND_UNSUPPORTED");
      reject_diagnostic([&] { (void)calls::parse(program); },
                        "SERIAL_ACTOR_BACKEND_UNSUPPORTED");
    }
    // A source HTTP actor must never downgrade to a scalar callable just
    // because the host has mailbox primitives. Verify the actual stage-0
    // parser's typed rejection for both equivalent serial spellings and 3x4.
    for (const char* program : {
      "define serial http actor Endpoint as end",
      "define http actor Endpoint as int requestConcurrency = 1; int messageConcurrency = 1; end",
      "define http actor Endpoint as int requestConcurrency = 3; int messageConcurrency = 4; end",
      "pub define serial http actor Endpoint as end",
      "define pub http actor Endpoint as end"
    }) {
      reject_diagnostic([&] { (void)parse_source(program); },
                        "HTTP_ACTOR_BACKEND_UNSUPPORTED");
      reject_diagnostic([&] { (void)calls::parse(program); },
                        "HTTP_ACTOR_BACKEND_UNSUPPORTED");
    }
    // Exercise identical bytes with BOTH independent Forge source compilers.
    // CMake copies these into the test build working directory, so this
    // cannot accidentally pass by looking at a mutable relative source path.
    for (const auto& [name, expected] : std::array<std::pair<const char*,const char*>,3>{{
      {"serial-1x1.atto",
       "define pub serial http actor Endpoint as\n"
       "  receive(ActorMail<int> mail): void { return; }\nend\n"},
      {"explicit-1x1.atto",
       "define pub http actor Endpoint as\n"
       "  int requestConcurrency = 1;\n  int messageConcurrency = 1;\n"
       "  receive(ActorMail<int> mail): void { return; }\nend\n"},
      {"unsupported-3x4.atto",
       "define pub http actor Endpoint as\n"
       "  int requestConcurrency = 3;\n  int messageConcurrency = 4;\n"
       "  receive(ActorMail<int> mail): void { return; }\nend\n"}
    }}) {
      std::ifstream input(std::string("http-actor-v1/")+name,std::ios::binary);
      ensure(static_cast<bool>(input),"missing canonical HTTP actor Atto fixture");
      const std::string data((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
      ensure(data==expected,"canonical HTTP actor source bytes drifted");
      reject_diagnostic([&] { (void)parse_source(data); },
                        "HTTP_ACTOR_BACKEND_UNSUPPORTED");
      reject_diagnostic([&] { (void)calls::parse(data); },
                        "HTTP_ACTOR_BACKEND_UNSUPPORTED");
    }
    ensure(interpret(parse_source(
      "fnc main():int{return 42;} // define serial http actor NotReal as end"))==42,
      "HTTP actor comment must remain lexical trivia");
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
                      "runtime ownership operation requires typed lowering in Forge");
    reject_diagnostic([] { parse_source("fnc main():int{return rt mut borrow value;}"); },
                      "runtime ownership operation requires typed lowering in Forge");
    // Shared mutation cannot silently degrade to a scalar alias. The scalar
    // compiler and the opt-in runtime-entry profile must diagnose the feature
    // itself before any interpreted result, IR, or executable is produced.
    constexpr std::array<std::string_view,8> mut_share_invalid{
      "rt mut share 42",
      "(rt mut share 42)",
      "1+(rt mut share 42)",
      "rt mut share (rt borrow 42)",
      "rt mut share (rt mut share 42)",
      "rt mut share (40+2)",
      "rt /* boundary */ mut /* owner */ share 42",
      "rt\nmut\nshare 42",
    };
    for (const auto expression : mut_share_invalid) {
      const auto source_text=std::string("fnc main():int{return ")+
                             std::string(expression)+";}";
      reject_diagnostic([&] { (void)parse_source(source_text); },
                        "native rt mut share requires verified SharedMutex<T> capability");
      reject_diagnostic([&] { (void)parse_runtime_source(source_text); },
                        "native rt mut share requires verified SharedMutex<T> capability");
    }
    ensure(interpret(parse_source("fnc main():int{return 42;} // rt mut share ignored"))==42,
           "ownership keywords in trailing comment cannot affect scalar result");
    ensure(interpret(parse_source("fnc main():int{/* rt mut share ignored */ return 42;}"))==42,
           "ownership keywords in block comments cannot affect scalar result");

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
    // quantum is reserved with cpu/gpu: every source position, including
    // function and parameter names, fails before lowering.
    reject_diagnostic([] {
      parse_source("pub fnc main():int{return quantum { 42 };}");
    }, "execution region requires verified CPU/GPU Task lowering");
    reject_diagnostic([] {
      parse_source("fnc quantum(a:int):int{return a+1;}\nfnc main():int{return quantum(41);}");
    }, "execution region requires verified CPU/GPU Task lowering");
    reject_diagnostic([] {
      parse_source("fnc f(quantum:int):int{return quantum+1;}\nfnc main():int{return f(41);}");
    }, "execution region requires verified CPU/GPU Task lowering");
    ensure(interpret(parse_source("fnc f(quantum_bits:int):int{return quantum_bits+1;}\nfnc main():int{return f(41);}"))==42,
           "quantum-prefixed identifier stays ordinary");
    reject_diagnostic([] {
      parse_source("pub fnc main():int{return task { 42 };}");
    }, "execution task requires scheduler-aware Future/continuation lowering");
    reject_diagnostic([] {
      parse_source("pub fnc main():int{return nb { 42 };}");
    }, "execution region requires verified CPU/GPU Task lowering");

    // Both source frontends must validate bytes before ignoring comments.
    // A lexical skip is not permission to admit malformed source encodings.
    const std::string callable_comment="fnc main():int{return 1;} // ";
    for (const std::string& bad : {
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
    // Typed predicates must remain verifiable even for externally supplied IR.
    auto typed=calls::parse("fnc main(x:int):int{^ x eq 0 ? 42 : 7;}");
    ensure(calls::interpret(typed,std::array<std::int64_t,1>{0})==42,"typed ternary true");
    ensure(calls::interpret(typed,std::array<std::int64_t,1>{1})==7,"typed ternary false");
    auto negated=calls::parse("fnc main(x:int):int{return unless x !eq 0 then 42 else 7 fi;}");
    ensure(calls::interpret(negated,std::array<std::int64_t,1>{0})==42,"negation true");
    ensure(calls::interpret(negated,std::array<std::int64_t,1>{1})==7,"negation false");
    auto forged_pred=calls::parse("fnc main():int{return true ? 42 : 7;}");
    forged_pred.functions[0].nodes[0].kind=calls::Kind::Constant;
    reject_diagnostic([&] { calls::verify(forged_pred); },"condition_not_bool");
    auto forged_bool=calls::parse("fnc main():int{return true ? 42 : 7;}");
    forged_bool.functions[0].nodes[0].value=2;
    reject_diagnostic([&] { calls::verify(forged_bool); },"invalid Boolean literal");
    auto forged_arm=calls::parse("fnc main():int{return true ? 1 : 0;}");
    forged_arm.functions[0].nodes[1].kind=calls::Kind::Boolean;
    reject_diagnostic([&] { calls::verify(forged_arm); },"conditional branch is not int");
    auto forged_mixed=calls::parse("fnc main():int{return true eq false ? 42 : 7;}");
    forged_mixed.functions[0].nodes[1].kind=calls::Kind::Constant;
    reject_diagnostic([&] { calls::verify(forged_mixed); },"comparison operand type mismatch");
    auto forged_negation=calls::parse("fnc main():int{return unless true then 42 else 7 fi;}");
    forged_negation.functions[0].nodes[0].kind=calls::Kind::Constant;
    reject_diagnostic([&] { calls::verify(forged_negation); },"condition_not_bool");
    auto forged_logical=calls::parse("fnc main():int{return true && false ? 42 : 7;}");
    forged_logical.functions[0].nodes[1].kind=calls::Kind::Constant;
    reject_diagnostic([&] { calls::verify(forged_logical); },"condition_not_bool");
    auto guard=calls::parse("fnc main(int x):int{guard x>0 {^ 1;} guard x neq 5 else return 2; end return 42;}");
    ensure(calls::interpret(guard,std::array<std::int64_t,1>{0})==1,"guard first failure exits callable");
    ensure(calls::interpret(guard,std::array<std::int64_t,1>{5})==2,"guard second failure exits callable");
    ensure(calls::interpret(guard,std::array<std::int64_t,1>{3})==42,"guard success continues");
    auto guard_wrong_type=calls::parse("fnc main():int{guard true {return 1;} return 42;}");
    guard_wrong_type.functions[0].nodes[0].kind=calls::Kind::Constant;
    reject_diagnostic([&] { calls::verify(guard_wrong_type); },"condition_not_bool");
    auto bool_xor=calls::parse("fnc main(x:int):int{return Xor(x eq 0,false) ? 42 : 7;}");
    ensure(calls::interpret(bool_xor,std::array<std::int64_t,1>{0})==42,"Boolean Xor true");
    ensure(calls::interpret(bool_xor,std::array<std::int64_t,1>{1})==7,"Boolean Xor false");
    auto integer_xor=calls::parse("fnc main(a:int,b:int):int{return a ^^ b;}");
    ensure(calls::interpret(integer_xor,std::array<std::int64_t,2>{11,4})==15,"integer XOR");
    auto bad_integer_xor=calls::parse("fnc main():int{return 11 ^^ 4;}");
    bad_integer_xor.functions[0].nodes[1].kind=calls::Kind::Boolean;
    bad_integer_xor.functions[0].nodes[1].value=1;
    reject_diagnostic([&] { calls::verify(bad_integer_xor); },"Boolean is not an int");
    auto bad_xor=calls::parse("fnc main():int{return Xor(true,false) ? 42 : 7;}");
    bad_xor.functions[0].nodes[1].kind=calls::Kind::Constant;
    reject_diagnostic([&] { calls::verify(bad_xor); },"Boolean Xor requires two bool operands");
    auto result_bind=calls::parse("fnc divide(a:int,b:int):Result<int,Error>{return trap! (a/b);} fnc main(a:int,b:int):Result<int,Error>{val q=divide(a,b)?;return trap! (q+q);}");
    const auto rb_ok=calls::interpret_result(result_bind,std::array<std::int64_t,2>{6,3});
    const auto rb_err=calls::interpret_result(result_bind,std::array<std::int64_t,2>{1,0});
    ensure(rb_ok.value==4 && rb_ok.status==calls::ArithmeticStatus::Ok,"Result local success");
    ensure(rb_err.status==calls::ArithmeticStatus::DivisionByZero,"Result propagation retains code");
    auto forged_local=result_bind;
    for(auto& node:forged_local.functions[1].nodes)
      if(node.kind==calls::Kind::Local) node.value=-1;
    reject_diagnostic([&] { calls::verify(forged_local); },"invalid scalar Result local use");
    auto forged_result=result_bind;
    for(auto& node:forged_result.functions[1].nodes)
      if(node.kind==calls::Kind::ResultCall) node.kind=calls::Kind::Call;
    reject_diagnostic([&] { calls::verify(forged_result); },"scalar Result call type mismatch");
    auto option=calls::parse("fnc main(x:int):Option<int>{return if x eq 0 then Some(x) else None fi;}");
    const auto option_some=calls::interpret_option(option,std::array<std::int64_t,1>{0});
    const auto option_none=calls::interpret_option(option,std::array<std::int64_t,1>{1});
    ensure(option_some.value==0 && option_some.tag==1,"Some zero is not None");
    ensure(option_none.value==0 && option_none.tag==0,"None payload is zeroed");
    auto bad_option=option;
    bad_option.functions[0].nodes.back().kind=calls::Kind::If;
    reject_diagnostic([&] { calls::verify(bad_option); },"invalid scalar Option return");
    auto matched=calls::parse("fnc main():int{return match<int> Some(42) over on Some(n):int -> n, on None:int -> 0, end;}");
    ensure(calls::interpret(matched,{})==42,"Option match payload");
    auto forged_match=matched;
    std::swap(forged_match.functions[0].nodes.back().children[1],forged_match.functions[0].nodes.back().children[2]);
    reject_diagnostic([&] { calls::verify(forged_match); },"Option payload outside Some arm");
    auto result_match=calls::parse("fnc main():int{return match<int> Ok(42) over on Ok(n):int -> n, on Err():int -> 0, end;}");
    ensure(calls::interpret(result_match,{})==42,"scalar Result match consumes successful payload");
    auto flat_ok=calls::parse("fnc main():int{return match<int> trap! Ok(42) over on Ok(n):int -> n, on Err(_):int -> 0, end;}");
    ensure(calls::interpret(flat_ok,{})==42,"trap Ok stays flat");
    auto flat_fault=calls::parse("fnc main():int{return match<int> trap! Ok(1/0) over on Ok(n):int -> n, on Err(_):int -> 42, end;}");
    ensure(calls::interpret(flat_fault,{})==42,"trap Ok captures ordinary payload effect");
    auto forged_result_match=result_match;
    auto& match_node=forged_result_match.functions[0].nodes.back();
    std::swap(match_node.children[1],match_node.children[2]);
    reject_diagnostic([&] { calls::verify(forged_result_match); },"Result payload outside Ok arm");
    auto forged_result_subject=result_match;
    auto& subject=forged_result_subject.functions[0].nodes.at(
        static_cast<std::size_t>(forged_result_subject.functions[0].nodes.back().children[0]));
    subject.kind=calls::Kind::Some;
    reject_diagnostic([&] { calls::verify(forged_result_subject); },"Result match requires Result subject");
    auto bad_ok=calls::parse("fnc main(x:int):Result<int,Error>{return Ok(x);}");
    bad_ok.functions[0].nodes[0].kind=calls::Kind::Boolean;
    bad_ok.functions[0].nodes[0].value=1;
    reject_diagnostic([&] { calls::verify(bad_ok); },"constructor requires int payload");
    auto effect=calls::parse("fnc main(a:int):Result<int,Error>{return Ok(a+1);}");
    const auto escaped=calls::interpret_effect(effect,std::array<std::int64_t,1>{9223372036854775807LL});
    ensure(escaped.value==0 && escaped.tag==0 && escaped.effect==calls::ArithmeticStatus::Overflow,
           "ordinary effect does not become Err");
    auto captured=calls::parse("fnc main(a:int):Result<int,Error>{return trap! (a+1);}");
    const auto err=calls::interpret_effect(captured,std::array<std::int64_t,1>{9223372036854775807LL});
    ensure(err.value==0 && err.tag==65537 && err.effect==calls::ArithmeticStatus::Ok,
           "trap converts ordinary effect into Err");
    auto bool_helper=calls::parse(
      "use namespace types; pub fnc fits(int x):bool{return x>=-128 && x<=127;}"
      " fnc main(int x):int{return fits(x) ? 1 : 0;}");
    ensure(calls::interpret(bool_helper,std::array<std::int64_t,1>{-128})==1,
           "typed helper preserves inclusive bound");
    ensure(calls::interpret(bool_helper,std::array<std::int64_t,1>{128})==0,
           "typed helper rejects out of range");
    auto wrong_bool_return=bool_helper;
    wrong_bool_return.functions[0].returns_bool=false;
    reject_diagnostic([&] { calls::verify(wrong_bool_return); },"callable int return cannot be Boolean or Result");
    auto wrong_bool_call=bool_helper;
    for(auto& node:wrong_bool_call.functions[1].nodes)
      if(node.kind==calls::Kind::BoolCall) node.kind=calls::Kind::Call;
    reject_diagnostic([&] { calls::verify(wrong_bool_call); },"Boolean target type mismatch");
    auto wrong_bool_target=bool_helper;
    for(auto& node:wrong_bool_target.functions[1].nodes)
      if(node.kind==calls::Kind::BoolCall) {node.callee=1;node.name="main";}
    reject_diagnostic([&] { calls::verify(wrong_bool_target); },"Boolean target type mismatch");
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
    const auto identity=parse_runtime_source("fnc main(a:int):int{return a;}");
    const auto x86_identity=emit_runtime_asm(identity,Arch::X86_64);
    const auto arm_identity=emit_runtime_asm(identity,Arch::ARM64);
    ensure(x86_identity.find("movq %rdi, %rax")!=std::string::npos &&
           x86_identity.find("%rsp")==std::string::npos,
           "x86 incoming identity has no reference-storage slot or frame");
    ensure(arm_identity.find("  ret\\n")!=std::string::npos &&
           arm_identity.find("mov x0, x0")==std::string::npos &&
           arm_identity.find("[sp")==std::string::npos &&
           arm_identity.find("sub sp")==std::string::npos,
           "arm incoming identity needs no moves or frame");
    const auto forwarded=parse_runtime_source(
      "fnc forward(a:int,b:int,c:int,d:int):int{return d-c+b-a;}"
      "fnc main(a:int,b:int,c:int,d:int):int{return forward(a,b,c,d);}");
    ensure(interpret(forwarded,std::array<std::int64_t,4>{1,2,3,44})==42,
           "four incoming values preserve formal and operand order");
    const auto forwarded_x86=emit_runtime_asm(forwarded,Arch::X86_64);
    const auto forwarded_arm=emit_runtime_asm(forwarded,Arch::ARM64);
    ensure(forwarded_x86.find("movq %rdi, ")==std::string::npos &&
           forwarded_x86.find("subq %rdi, %rax")!=std::string::npos &&
           forwarded_x86.find("subq %rdx, %rax")!=std::string::npos,
           "x86 forwards original incoming registers");
    ensure(forwarded_arm.find("str x0,")==std::string::npos &&
           forwarded_arm.find("str x1,")==std::string::npos &&
           forwarded_arm.find("str x2,")==std::string::npos &&
           forwarded_arm.find("str x3,")==std::string::npos &&
           forwarded_arm.find("mov x10, x0")!=std::string::npos,
           "arm forwards original incoming registers");
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
      reject_diagnostic([&] { source(expression); }, "FORGE_CHECKED_DIVISION_BACKEND_UNSUPPORTED");
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
    // Portable Greek identifiers are admitted; Latin accents and NBSP remain rejected.
    // This must be stable under every process locale.
    reject_diagnostic([] {
      parse_source("fnc main():int {return caf\xc3\xa9;}");
    }, "invalid character");
    reject_diagnostic([] {
      parse_source("fnc main():int {return 1\xc2\xa0;}");
    }, "invalid character");
    reject_diagnostic([] {
      parse_source("fnc main():int {return \xce\xbb;}");
    }, "expected integer");
    ensure(interpret(parse_source("fnc id(\xce\xbb:int):int{return \xce\xbb;} fnc main():int{return id(42);}"))==42,
           "portable Greek parameter executes");
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
      reject_diagnostic([&] { (void)parse_source(candidate); },
          std::string_view(candidate).find("do match")!=std::string_view::npos
              ? "unexpected token" // rejected at do, before lexing match
              : std::string_view(candidate).find("match 1 over")!=std::string_view::npos
                  ? "MATCH_DEFAULT_BACKEND_UNSUPPORTED" : "expected");
    }
    reject_diagnostic([] {
      (void)parse_source("@structural\nfnc main():int{return 1;}");
    }, "annotations require");
    for (const auto& attribute : {
        "@FromJson(\"id\")", "@CallerRole(Admin)", "@CallableBy(Admin)",
        "@DefaultArgVals(x=1)", "@UnknownPolicy"}) {
      reject_diagnostic([&] {
        (void)parse_source(std::string(attribute)+"\nfnc main():int{return 1;}");
      }, "annotations require");
      reject_diagnostic([&] {
        (void)calls::parse(std::string(attribute)+"\nfnc main():int{return 1;}");
      }, "annotations require");
    }
    ensure(interpret(parse_source("fnc main():int{return 42;} // @CallerRole(Admin)"))==42,
           "annotation-looking text in comments is not source metadata");
    reject_diagnostic([] {
      parse_source("structural fnc main():int{return 1;}");
    }, "expected");
    reject_diagnostic([] {
      parse_source("fnc main(structural Shape value):int{return 1;}");
    }, "tuple-v2 parameter requires typed ownership/ABI lowering");
    reject_diagnostic([] {
      parse_source("fnc main():int{match 1 over on _ -> {return 1;} end}");
    }, "MATCH_DEFAULT_BACKEND_UNSUPPORTED");
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
