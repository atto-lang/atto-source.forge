#include "compiler/nominal_scalar.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr std::string_view kBox = R"ores(
define class Box as
  let int value = 40;
  add(mut self, int n): void {
    self.value = self.value + n;
  }
  read(self): int {
    return self.value;
  }
end
pub fnc main(): int {
  let mut Box b = new Box();
  b.add(2);
  return b.read();
}
)ores";

void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
void rejects(std::string_view source, std::string_view diagnostic = {}) {
  try {
    (void)oresforge::nominal::parse(source);
  } catch (const std::runtime_error& error) {
    if (!diagnostic.empty() &&
        std::string_view(error.what()).find(diagnostic) ==
            std::string_view::npos)
      throw std::runtime_error("wrong nominal rejection: " +
                               std::string(error.what()));
    return;
  }
  throw std::runtime_error("unsupported nominal source was accepted");
}
void overflow(const oresforge::nominal::ScalarClass& program) {
  try {
    (void)oresforge::nominal::interpret(program);
  } catch (const std::runtime_error& e) {
    require(std::string_view(e.what()) == "i64 add overflow",
            "overflow failed for the wrong reason");
    return;
  }
  throw std::runtime_error("i64 overflow was silently accepted");
}
} // namespace

int main() {
  try {
    using oresforge::nominal::parse;
    using oresforge::nominal::interpret;
    using oresforge::nominal::emit_x86_64_asm;
    const auto box = parse(kBox);
    require(box.owner == "Box" && box.field == "value" &&
            box.writer == "add" && box.reader == "read" &&
            box.initializer == 40 && box.argument == 2,
            "bounded nominal source was not fully parsed");
    require(interpret(box) == 42, "class write/read did not return 42");
    const auto native = emit_x86_64_asm(box);
    require(native.find(".globl oresforge_entry") != std::string::npos &&
            native.find("subq $16, %rsp") != std::string::npos &&
            native.find("addq %rdx, %rax") != std::string::npos &&
            native.find("jo .Loresforge_nominal_overflow") != std::string::npos &&
            native.find("ud2") != std::string::npos,
            "class codegen omitted stack ownership or native overflow trap");
    // Both contextual names and explicit/legacy declaration markers resolve
    // through the very same receiver-erased method identity.
    const std::string prefix = "define class Box as let int value=40; ";
    const std::string writer = "add(mut self,int n):void{self.value=self.value+n;} ";
    const std::string reader = "():int{return self.value;} ";
    const std::string mainStart =
        "end fnc main():int{let mut Box b=new Box();b.add(2);return b.";
    for (const std::string spelling : {"", "mtd ", "fnc "}) {
      for (const std::string name : {"read", "mtd", "method"}) {
        const auto program = parse(prefix + spelling + writer +
            spelling + name + reader + mainStart + name + "();}");
        require(interpret(program) == 42, "instance marker changed guest value");
        require(program.methods[0].name == "add" &&
                program.methods[1].name == name &&
                program.methods[1].value_arity == 0,
                "instance marker changed method identity");
        require(emit_x86_64_asm(program) == native,
                "equivalent method spelling changed native instructions");
        if (!spelling.empty()) {
          rejects(prefix + writer + name + reader + spelling + name + reader +
              mainStart + name + "();}", "duplicate native method name/arity");
          rejects(prefix + writer + spelling + name + reader + name + reader +
              mainStart + name + "();}", "duplicate native method name/arity");
        }
      }
    }
    for (const std::string name : {"read", "mtd", "method"}) {
      rejects(prefix + writer + "method " + name + reader +
          mainStart + name + "();}", "obsolete method introducer");
    }
    rejects(prefix + "method " + writer + "read" + reader +
        mainStart + "read();}", "obsolete method introducer");
    rejects(prefix + writer + "static mtd read" + reader + mainStart + "read();}");
    rejects(prefix + writer + "mtd fnc read" + reader + mainStart + "read();}");
    rejects(prefix + writer + "fnc()" + reader + mainStart + "read();}");
    require(interpret(parse(prefix + "mtd /* marker */ " + writer +
        "mtd /* name */ mtd" + reader + mainStart + "mtd();}")) == 42,
        "comments changed contextual method marker admission");
    // Fieldless profile uses the same receiver-erased slots and checked
    // scalar lowering, but needs no guest stack slot for the receiver.
    for (const std::string name : {"ping", "mtd", "method"}) {
      std::string baseline;
      for (const std::string marker : {"", "mtd ", "fnc "}) {
        for (const std::string form : {"(int n)", "(self,int n)", "(self)(int n)"}) {
          const auto src = "define class C as pub " + marker + name + form +
              ":int{return n+2;} end pub fnc main():int{return new C()." + name + "(40);}";
          const auto program = parse(src);
          require(interpret(program)==42, "fieldless receiver guest value");
          const auto code = emit_x86_64_asm(program);
          if (baseline.empty()) baseline = code;
          require(code==baseline && code.find("%rsp")==std::string::npos,
                  "fieldless method spelling changed native lowering or allocated receiver storage");
        }
      }
    }
    rejects("define class C as ping():int{return 42;} end fnc main():int{return new C().ping();}",
            "private fieldless method");
    rejects("define class C as pub ping(self,):int{return 42;} end fnc main():int{return new C().ping();}",
            "trailing receiver comma");
    const auto fieldless = parse("define class C as pub ping(int n):int{return n+1;} end fnc main():int{return new C().ping(9223372036854775807);}");
    overflow(fieldless);
    require(emit_x86_64_asm(fieldless).find("jo .Loresforge_nominal_overflow")!=std::string::npos,
            "fieldless checked addition omitted overflow trap");
    // Abstract contracts are checked before erasure; native code is identical
    // to the concrete fieldless profile, with no runtime interface object.
    const std::string concrete = "define class C as pub ping(int n):int{return n+2;} end "
        "pub fnc main():int{return new C().ping(40);}";
    const auto concrete_code = emit_x86_64_asm(parse(concrete));
    for (const std::string kind : {"interface", "trait"}) {
      for (const std::string marker : {"", "mtd ", "fnc "}) {
        const auto source = "define " + kind + " Required as " + marker +
            "ping(self)(int n):int; end define class C implements Required as "
            "pub ping(int n):int{return n+2;} end pub fnc main():int{return new C().ping(40);}";
        const auto program = parse(source);
        require(interpret(program)==42 && emit_x86_64_asm(program)==concrete_code,
                "contract changed native lowering");
      }
    }
    const std::string contract = "define interface Required as ping(int n):int; end ";
    const std::string main = " end pub fnc main():int{return new C().ping(40);}";
    rejects(contract+"define class C implements Required as"+main, "MISSING_INSTANCE_OBLIGATION");
    rejects(contract+"define class C implements Required as ping(int n):int{return n+2;}"+main,
            "E_IMPL_VISIBILITY");
    rejects(contract+"define class C implements Required as pub ping(int x):int{return x+2;}"+main,
            "E_IMPL_SIGNATURE");
    rejects(contract+"define class C implements Missing as pub ping(int n):int{return n+2;}"+main,
            "E_IMPL_UNRESOLVED");
    rejects(contract+"define class C implements Required,Required as pub ping(int n):int{return n+2;}"+main,
            "duplicate implements entry");
    rejects(contract+concrete,"E_CONTRACT_PROFILE");
    rejects("pub "+contract+concrete,"public contract unsupported");
    rejects(contract+"define trait Required as ping(int n):int; end "+concrete,"duplicate nominal declaration");
    rejects("define trait Required as ping(int n):int; mtd ping(self,int n):int; end "+concrete,
            "duplicate native method name/arity");
    rejects("define trait Required as ping(int n):int{return n+2;} end "+concrete);
    rejects(contract+"define class C implements Required as let int value=40; end "+main,
            "stateful contract implementation unsupported");
    const auto split = parse(R"ores(
      define class Counter as let int count=40;
        read():int{return self.count;}
        add(mut self)(int increment):void {
          self.count=self.count+increment;
          return;
        }
      end
      define pub fnc main()->int{
        let mut Counter item=new Counter();
        item.add(2);return item.read();
      }
    )ores");
    require(interpret(split) == 42, "split mutable receiver or implicit self");
    require(interpret(parse(R"ores(
      define class Box as let int value=40;
        add(mut self, int n):void{self.value=self.value+n;}
        read(self):int{return self.value;}
      end
      fnc main():int{let mut Box b=new Box();b.add(3);return b.read();}
    )ores")) == 43, "native class method argument was constant-forged");
    // Same class image, two independent stack-owned instances, with method
    // identities resolved by name and caller-visible value arity.
    const auto multi = parse(R"ores(
      define class Counter as let int value=40;
        add(mut self, int n):void{self.value=self.value+n;}
        subtract(mut self)(int n):void{self.value=self.value-n;}
        read(self):int{return self.value;}
        get():int{return self.value;}
      end
      fnc main():int{
        let mut Counter a=new Counter();
        let mut Counter b=new Counter();
        a.add(8);
        b.subtract(10);
        a.subtract(6);
        b.add(12);
        return a.get();
      }
    )ores");
    require(multi.instances.size() == 2 && multi.methods.size() == 4 &&
            multi.steps.size() == 7 && interpret(multi) == 42,
            "receiver method IR shared body or independent instance slots drifted");
    const auto multiNative = emit_x86_64_asm(multi);
    require(multiNative.find("8(%rsp)") != std::string::npos &&
            multiNative.find("subq %rdx, %rax") != std::string::npos,
            "native code omitted independent instance or subtraction method");
    auto tampered = multi;
    tampered.steps[3].instance = 9;
    try {
      (void)interpret(tampered);
      throw std::runtime_error("tampered method IR receiver was accepted");
    } catch (const std::runtime_error& error) {
      require(std::string_view(error.what()).find("invalid nominal instance slot") !=
              std::string_view::npos, "tampered receiver failed for wrong reason");
    }
    // Never trust an externally constructed or fuzzed nominal IR value.
    // Both the interpreter and native codegen independently validate it.
    for (int variant=0; variant<4; ++variant) {
      auto malformed=multi;
      if (variant==0) malformed.methods[0].kind=static_cast<oresforge::nominal::Method::Kind>(255);
      if (variant==1) malformed.steps[2].kind=static_cast<oresforge::nominal::Step::Kind>(255);
      if (variant==2) malformed.steps[0].argument=9;
      if (variant==3) malformed.steps.back().argument=1;
      bool interpreterRejected=false, codegenRejected=false;
      try { (void)interpret(malformed); } catch (const std::runtime_error&) {
        interpreterRejected=true;
      }
      try { (void)emit_x86_64_asm(malformed); } catch (const std::runtime_error&) {
        codegenRejected=true;
      }
      require(interpreterRejected && codegenRejected,
              "malformed nominal IR was accepted by interpreter or codegen");
    }
    rejects(R"ores(
      define class Box as let int value=1;
        x(mut self,int n):void{self.value=self.value+n;}
        x(mut self)(int n):void{self.value=self.value-n;}
        read(self):int{return self.value;}
      end
      fnc main():int{let mut Box b=new Box();return b.read();}
    )ores", "duplicate native method name/arity");
    rejects(R"ores(
      define class Box as let int value=1;
        add(mut self,int n):void{self.value=self.value+n;}
        read():int{return self.value;}
      end
      fnc main():int{b.add(2);let mut Box b=new Box();return b.read();}
    )ores", "unknown or uninitialized class receiver");
    rejects(R"ores(
      define class Box as let int value=1;
        add(mut self,int n):void{self.value=self.value+n;}
        read():int{return self.value;}
      end
      fnc main():int{let mut Box b=new Box();let mut Box b=new Box();return b.read();}
    )ores", "duplicate or invalid class binding");
    rejects(R"ores(
      define class Box as let int value=1;
        add(mut self,int n):void{self.value=self.value+n;}
        read():int{return self.value;}
      end
      fnc main():int{let mut Box b=new Box();b.read(2);return b.read();}
    )ores", "unknown nominal method or caller-visible arity");
    auto p = box;
    p.initializer = std::numeric_limits<std::int64_t>::max();
    p.argument = 1;
    overflow(p);
    // Even if the known fixture overflows, the emitted native code must
    // include the runtime trap, not unguarded wrapping arithmetic.
    require(emit_x86_64_asm(p).find("jo .Loresforge_nominal_overflow") !=
            std::string::npos, "native overflow trap was omitted");

    rejects("define actor Box as end fnc main():int{return 42;}");
    rejects("define class Box as let int value=40; end fnc main():int{return 42;}",
            "bounded class requires one mutable writer and one reader");
    rejects("define class Box as let int value=40; add(mut self,int n):void"
            "{self.value=self.value+n;} read(self):int{return self.value;} end "
            "fnc main():int{let Box b=new Box();b.add(2);return b.read();}");
    rejects("define class Box as let int value=40; add(mut self,int n):void"
            "{self.value=self.value+n;} read(self):int{return self.value;} end "
            "fnc main():int{let mut Box b=new Box();b.add(2);return b.read();}"
            "fnc extra():int{return 100;}", "unsupported trailing nominal source");
    rejects("define class Box as let int value=40; add(mut self,int n):void"
            "{self.value=self.value+n;} read(self):int{return self.value;} "
            "end fnc main():int{let mut Box b=new Box();b.add(2);return b.read();}"
            "/* unterminated comment", "unterminated nominal comment");
    rejects("define class Box as let int value=40; add(mut self,int n):void"
            "{self.value=self.value+n;} read(self):int{return self.value;} "
            "end fnc main():int{let mut Box b=new Box();b.add(2);return b.read();}"
            " @", "invalid nominal source character");
    std::cout << "bounded nominal scalar class contract: PASS\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "bounded nominal scalar class contract: FAIL: "
              << e.what() << '\n';
    return 1;
  }
}
