#!/usr/bin/env python3
"""Real native x86-64 call/return, lazy branching and recursion proof.

The stage-0 compiler itself is C++ and this is not self-hosting.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import tempfile
import hashlib

# Each tuple is (arguments..., value, typed variant, escaping ordinary effect).
EFFECT_CASES = (
    ("option-payload-effect", "option", "fnc main(a:int):Option<int>{return Some(a+1);}",
     ((0,1,1,0),(9223372036854775807,0,0,65537))),
    ("option-predicate-effect", "option", "fnc main(a:int):Option<int>{return if a+1 eq 0 then None else Some(a) fi;}",
     ((-1,0,0,0),(0,0,1,0),(9223372036854775807,0,0,65537))),
    ("option-helper-effect", "option", "fnc f(a:int,b:int):Option<int>{return Some(a/b);} fnc main(a:int,b:int):Option<int>{return f(a,b);}",
     ((42,2,21,1,0),(42,0,0,0,65538))),
    ("ok-payload-effect", "result", "fnc main(a:int):Result<int,Error>{return Ok(a+1);}",
     ((0,1,0,0),(9223372036854775807,0,0,65537))),
    ("result-forward-effect", "result", "fnc f(a:int,b:int):Result<int,Error>{return Ok(a/b);} fnc main(a:int,b:int):Result<int,Error>{return f(a,b);}",
     ((42,2,21,0,0),(42,0,0,0,65538))),
    ("trap-catches-forward-effect", "result", "fnc f(a:int,b:int):Result<int,Error>{return Ok(a/b);} fnc main(a:int,b:int):Result<int,Error>{return trap! f(a,b);}",
     ((42,2,21,0,0),(42,0,0,65538,0))),
    ("trap-preserves-existing-err", "result", "fnc f(a:int,b:int):Result<int,Error>{return trap! (a/b);} fnc main(a:int,b:int):Result<int,Error>{return trap! f(a,b);}",
     ((42,2,21,0,0),(42,0,0,65538,0))),
    ("question-does-not-catch-effect", "result", "fnc f(a:int,b:int):Result<int,Error>{return Ok(a/b);} fnc main(a:int,b:int):Result<int,Error>{val q=f(a,b)?; return Ok(q);}",
     ((42,2,21,0,0),(42,0,0,0,65538))),
    ("question-propagates-err", "result", "fnc f(a:int,b:int):Result<int,Error>{return trap! (a/b);} fnc main(a:int,b:int):Result<int,Error>{val q=f(a,b)?; return Ok(q);}",
     ((42,2,21,0,0),(42,0,0,65538,0))),
    ("argument-effect", "result", "fnc f(a:int):Result<int,Error>{return Ok(a);} fnc main(a:int):Result<int,Error>{val q=f(a+1)?; return Ok(q);}",
     ((0,1,0,0),(9223372036854775807,0,0,65537))),
    ("trap-argument-effect", "result", "fnc f(a:int):Result<int,Error>{return Ok(a);} fnc main(a:int):Result<int,Error>{return trap! f(a+1);}",
     ((0,1,0,0),(9223372036854775807,0,65537,0))),
    ("effect-four-arguments", "int", "fnc main(a:int,b:int,c:int,d:int):int{return a+b*c/d;}",
     ((1,6,7,2,22,0,0),(1,6,7,0,0,0,65538))),
)
OPTION_CASES = (
    ("option-some", "fnc main(a:int):Option<int>{return Some(a);}",
     ((0,0,1),(-9223372036854775808,-9223372036854775808,1),(9223372036854775807,9223372036854775807,1))),
    ("option-none", "fnc main():Option<int>{return None;}", ((0,0),)),
    ("option-choice", "fnc main(a:int):Option<int>{return if a eq 0 then None else Some(a) fi;}",
     ((0,0,0),(-7,-7,1),(42,42,1))),
    ("option-forward", "fnc main(a:int):Option<int>{return find(a);} fnc find(a:int):Option<int>{return if a neq 5 then Some(a) else None fi;}",
     ((0,0,1),(5,0,0),(-5,-5,1))),
    ("option-multiframe", "fnc find(a:int):Option<int>{return Some(a);} fnc wrap(a:int):Option<int>{return find(a);} fnc main(a:int):Option<int>{return if a<0 then None else wrap(a) fi;}",
     ((-1,0,0),(0,0,1),(42,42,1))),
)
OPTION_INVALID = (
    ("option-int", "fnc main():Option<int>{return 42;}", "Option return requires"),
    ("option-bool", "fnc main():Option<int>{return Some(true);}", "Boolean is not an int"),
    ("option-int-condition", "fnc main():Option<int>{return if 1 then None else Some(0) fi;}", "condition_not_bool"),
    ("option-to-int", "fnc find():Option<int>{return None;} fnc main():int{return find();}", "scalar Option call type mismatch"),
    ("option-from-int", "fnc find():int{return 0;} fnc main():Option<int>{return find();}", "scalar Option call type mismatch"),
    ("option-trap-nesting", "fnc find():Option<int>{return None;} fnc main():Result<int,Error>{return trap! find();}", "scalar Option call type mismatch"),
    ("option-none-call", "fnc main():Option<int>{return None();}", "unexpected token"),
    ("option-nested", "fnc main():Option<Option<int>>{return None;}", "unexpected token"),
)
RESULT_CASES = (
    ("result-ok", "fnc main(a:int):Result<int,Error>{return Ok(a);}", ((0,0,0),(-7,-7,0),(42,42,0))),
    ("result-ok-propagate", "fnc divide(a:int,b:int):Result<int,Error>{return trap! (a/b);} fnc main(a:int,b:int):Result<int,Error>{val q=divide(a,b)?; return Ok(q);}",
     ((84,2,42,0),(1,0,0,65538),(-9223372036854775808,-1,0,65537))),
    ("result-ok-flat", "fnc f(a:int):Result<int,Error>{return Ok(a);} fnc main(a:int):Result<int,Error>{return trap! f(a);}", ((0,0,0),(42,42,0))),

    ("result-helper-propagate", "fnc divide(int a,int b):Result<int,Error>{return trap! (a/b);} fnc main(int a,int b):Result<int,Error>{val q=divide(a,b)?; return trap! (q*2);}",
     ((84,2,84,0),(-5,2,-4,0),(42,0,0,65538),(-9223372036854775808,-1,0,65537),(9223372036854775807,1,0,65537))),
    ("result-forward-call", "fnc main(a:int,b:int):Result<int,Error>{return divide(a,b);} fnc divide(a:int,b:int):Result<int,Error>{return trap (a/b);}",
     ((5,2,2,0),(1,0,0,65538),(-9223372036854775808,-1,0,65537))),
    ("result-flat-helper", "fnc divide(a:int,b:int):Result<int,Error>{return trap! (a/b);} fnc main(a:int,b:int):Result<int,Error>{return trap! divide(a,b);}",
     ((5,2,2,0),(1,0,0,65538),(-9223372036854775808,-1,0,65537))),
    ("result-local-multiple-use", "fnc divide(a:int,b:int):Result<int,Error>{return trap! (a/b);} fnc main(a:int,b:int):Result<int,Error>{val q=divide(a,b)?; return trap! (q+q);}",
     ((6,3,4,0),(1,0,0,65538),(9223372036854775807,1,0,65537))),
    ("result-skip-continuation", "fnc divide(a:int,b:int):Result<int,Error>{return trap! (a/b);} fnc main(a:int,b:int):Result<int,Error>{val q=divide(a,b)?; return trap! (9223372036854775807+1);}",
     ((1,0,0,65538),(1,1,0,65537))),
    ("result-multiple-frames", "fnc divide(a:int,b:int):Result<int,Error>{return trap! (a/b);} fnc twice(a:int,b:int):Result<int,Error>{val q=divide(a,b)?; return trap! (q*2);} fnc main(a:int,b:int):Result<int,Error>{val q=twice(a,b)?; return trap! (q+1);}",
     ((6,3,5,0),(1,0,0,65538),(-9223372036854775808,-1,0,65537))),

    ("trap-bang", "fnc main(a:int,b:int):Result<int,Error>{return trap! (a/b);}",
     ((5,2,2,0),(-5,2,-2,0),(42,0,0,65538),(-9223372036854775808,-1,0,65537))),
    ("trap-bare", "fnc main(a:int,b:int):Result<int,Error>{^ trap (a/b);}",
     ((5,2,2,0),(0,1,0,0),(42,0,0,65538),(-9223372036854775808,-1,0,65537))),
    ("trap-bang-tab", "fnc main(a:int,b:int):Result<int,Error>{return trap!\t(a/b);}",
     ((7,2,3,0),(-7,3,-2,0),(42,0,0,65538))),
    ("trap-bang-linebreak", "fnc main(a:int,b:int):Result<int,Error>{return trap!\n(a/b);}",
     ((5,2,2,0),(42,0,0,65538),(-9223372036854775808,-1,0,65537))),
    ("trap-flat", "fnc main(a:int,b:int):Result<int,Error>{return trap! trap (a/b);}",
     ((5,2,2,0),(42,0,0,65538),(-9223372036854775808,-1,0,65537))),
    ("trap-call", "fnc q(a:int,b:int):int{return a/b;} fnc wrap(a:int,b:int):int{return q(a,b)+1;} fnc main(a:int,b:int):Result<int,Error>{return trap! wrap(a,b);}",
     ((84,2,43,0),(42,0,0,65538),(-9223372036854775808,-1,0,65537),(9223372036854775807,1,0,65537))),
    ("trap-lazy", "fnc main(a:int):Result<int,Error>{return trap! (a eq 0 ? 42 : 84/a);}",
     ((0,42,0),(2,42,0),(-2,-42,0))),
    ("trap-order", "fnc main(a:int):Result<int,Error>{return trap! ((1/a)+(9223372036854775807+1));}",
     ((0,0,65538),(1,0,65537))),
)
RESULT_INVALID = (
    ("bang-adjacent-paren", "fnc main():Result<int,Error>{return trap!(7/2);}", "E_BANG_KEYWORD_WHITESPACE"),
    ("bang-adjacent-number", "fnc main():Result<int,Error>{return trap!42;}", "E_BANG_KEYWORD_WHITESPACE"),
    ("bang-adjacent-keyword", "fnc main():Result<int,Error>{return trap!trap 42;}", "E_BANG_KEYWORD_WHITESPACE"),
    ("bang-adjacent-comment", "fnc main():Result<int,Error>{return trap!//comment\n42;}", "E_BANG_KEYWORD_WHITESPACE"),
    ("copy-double-bang-adjacent", "fnc main():int{return copy!!42;}", "E_BANG_KEYWORD_WHITESPACE"),
    ("ok-bool", "fnc main():Result<int,Error>{return Ok(false);}", "Boolean is not an int"),
    ("err-numeric", "fnc main():Result<int,Error>{return Err(65538);}", "Err requires typed Error payload lowering"),

    ("trap-bang-block-comment", "fnc main():Result<int,Error>{return trap!/*gap*/ (84/2);}", "E_BANG_KEYWORD_WHITESPACE"),
    ("trap-bang-double", "fnc main():Result<int,Error>{return trap!! (84/2);}", "E_BANG_KEYWORD_WHITESPACE"),
    ("trap-bool", "fnc main():Result<int,Error>{return trap! true;}", "Boolean is not an int"),
    ("trap-int-return", "fnc main():int{return trap! 42;}", "reserved predicate word"),
    ("result-unwrapped", "fnc main():Result<int,Error>{return 42;}", "requires explicit trap"),
    ("result-to-int", "fnc f():Result<int,Error>{return trap! 42;} fnc main():int{return f();}", "scalar Result call type mismatch"),
    ("result-bind-int", "fnc f():int{return 42;} fnc main():Result<int,Error>{val q=f()?; return trap! q;}", "scalar Result call type mismatch"),
    ("result-bind-noncall", "fnc main():Result<int,Error>{val q=42?; return trap! q;}", "postfix ? requires a Result callable"),
    ("result-unbound-local", "fnc f(a:int):Result<int,Error>{return trap! a;} fnc main():Result<int,Error>{val q=f(q)?; return trap! q;}", "unknown parameter"),
    ("result-shadow-param", "fnc f():Result<int,Error>{return trap! 1;} fnc main(q:int):Result<int,Error>{val q=f()?; return trap! q;}", "Result binding shadows a parameter"),
    ("result-recursive", "fnc main():Result<int,Error>{return trap! main();}", "calls to scalar Result entry are unsupported"),
    ("trap-unary-precedence", "fnc main():Result<int,Error>{return trap! 5/2;}", "unexpected token"),
)
POSITIVE = (
    ("signed-division", "fnc main(a:int,b:int):int{return a/b;}",
     ((5,2,2),(-5,2,-2),(5,-2,-2),(-5,-2,2),(9007199254740993,1,9007199254740993),
      (-9223372036854775808,1,-9223372036854775808),(9223372036854775807,3,3074457345618258602),
      (0,-1,0),(-9223372036854775808,2,-4611686018427387904),
      (9223372036854775807,-1,-9223372036854775807))),
    ("division-precedence", "fnc main(a:int):int{return 2+a/3*4;}", ((8,10),(-8,-6))),
    ("division-lazy", "fnc main(a:int):int{return a eq 0 ? 42 : 84/a;}", ((0,42),(2,42),(-2,-42))),
    ("division-third-argument", "fnc q(a:int,b:int,c:int):int{return a/b+c;} fnc main(a:int,b:int,c:int):int{return q(a,b,c);}",
     ((84,2,7,49),(-5,2,42,40))),
    ("factorial", "fnc fact(n:int):int{return if n<=1 then 1 else n*fact(n-1);}"
     " fnc main(n:int):int{return fact(n);}",
     ((0,1),(1,1),(5,120),(10,3628800),(20,2432902008176640000))),
    ("fibonacci", "fnc fib(n:int)->int{return if n<=1 then n else fib(n-1)+fib(n-2);}"
     " fnc main(n:int):int{return fib(n);}",
     ((0,0),(1,1),(6,8),(12,144))),
    ("mutual", "fnc even(n:int):int{return if n==0 then 1 else odd(n-1);}"
     " fnc odd(n:int):int{return if n==0 then 0 else even(n-1);}"
     " fnc main(n:int):int{return even(n);}",
     ((0,1),(1,0),(6,1),(11,0))),
    ("nested", "fnc scale(x:int,y:int):int{return x*y;}"
     " fnc add(x:int,y:int):int{return x+y;}"
     " fnc main(a:int,b:int):int{return add(scale(a,2),scale(b,3));}",
     ((7,5,29),(-9,8,6),(0,0,0))),
    ("lazy", "fnc main(x:int):int{return if x>0 then 100+x else if x<0 then -100+x else 0;}",
     ((-5,-105),(0,0),(5,105))),
    ("signed-comparisons", "fnc main(a:int,b:int):int{return if a<b then 1 else if a>=b then 2 else 3;}",
     ((-9223372036854775808,9223372036854775807,1),(2,2,2),(10,2,2))),
    ("typed-equality", "fnc main(a:int,b:int):int{^ a eq b ? 42 : 7;}",
     ((0,0,42),(1,2,7),(-9223372036854775808,-9223372036854775808,42))),
    ("typed-neq", "fnc main(a:int,b:int):int{return if a neq b then 42 else 7;}",
     ((1,2,42),(2,2,7))),
    ("typed-bang-eq", "fnc main(a:int,b:int):int{return a !eq b ? 42 : 7;}",
     ((1,2,42),(2,2,7))),
    ("ifnot", "fnc main(a:int):int{return ifnot a eq 0 then 42 else 7 fi;}",
     ((-1,42),(0,7),(1,42))),
    ("unless", "fnc main(a:int):int{return unless a eq 0 then 42 else 7 fi;}",
     ((-1,42),(0,7),(1,42))),
    ("lazy-ternary", "fnc main(a:int):int{return a eq 0 ? 42 : 9223372036854775807+a;}",
     ((0,42),(-1,9223372036854775806))),
    ("lazy-negated", "fnc main(a:int):int{return unless a eq 1 then 42 else 9223372036854775807+a fi;}",
     ((2,42),(0,42))),
    ("bool-equality", "fnc main(a:int):int{return (a eq 0) eq true ? 42 : 7;}",
     ((0,42),(1,7))),
    ("nested-ternary", "fnc main(a:int):int{return a eq 0 ? 42 : a eq 1 ? 7 : 9;}",
     ((0,42),(1,7),(2,9))),
    ("logical-and-lazy", "fnc main(a:int):int{return a eq 0 && 9223372036854775807+a eq 0 ? 7 : 42;}",
     ((0,42),(1,42),(-1,42))),
    ("logical-or-lazy", "fnc main(a:int):int{return a eq 1 || 9223372036854775807+a eq 0 ? 42 : 7;}",
     ((1,42),(0,7),(-1,7))),
    ("logical-precedence", "fnc main(a:int):int{return a eq 0 || a eq 1 && false ? 42 : 7;}",
     ((0,42),(1,7),(2,7))),
    ("logical-nested", "fnc main(a:int):int{return ifnot (a eq 0 || a eq 1) && true then 42 else 7 fi;}",
     ((0,7),(1,7),(2,42))),
    ("guard-brace-continue", "// Same semantic behavior as keyword-continue.atto.\npub fnc main(): int {\n  guard true { return 0; }\n  return 42;\n}", ((42,),)),
    ("guard-brace-failure", "// False guard executes terminal failure block.\npub fnc main(): int {\n  guard false { return 0; }\n  return 42;\n}", ((0,),)),
    ("guard-keyword-continue", "// Same semantic behavior as brace-continue.atto.\npub fnc main(): int {\n  guard true else\n    return 0;\n  end\n  return 42;\n}", ((42,),)),
    ("guard-keyword-failure", "// False guard executes terminal failure block.\npub fnc main(): int {\n  guard false else\n    return 0;\n  end\n  return 42;\n}", ((0,),)),
    ("guard-canonical-scalar", "// Canonical AttoLang guard v1: successful guest exit status must be 42.\nfnc bounded(int x): int {\n  guard x > 0 { return 1; }\n  guard x < 10, x neq 5 { return 2; }\n  return 42;\n}\n\npub fnc main(): int {\n  return bounded(3);\n}", ((42,),)),
    ("guard-runtime", "fnc main(int a):int{guard a>0 {^ 1;} guard a<10, a neq 5 else return 2; end return 42;}",
     ((0,1),(3,42),(5,2),(11,2),(-1,1))),
    ("guard-lazy-checks", "fnc main(a:int):int{guard a eq 0, 9223372036854775807+a eq 0 {return 7;} return 42;}",
     ((0,7),(1,7),(-1,7))),
    ("guard-lazy-failure", "fnc main(a:int):int{guard a eq 1 {return 9223372036854775807+a;} return 42;}",
     ((1,42),(0,9223372036854775807))),
    ("guard-lazy-continuation", "fnc main(a:int):int{guard a eq 0 else return 42; end return 9223372036854775807+a;}",
     ((1,42),(0,9223372036854775807))),
    ("bool-xor-truth-table", "fnc main(a:int,b:int):int{return Xor(a eq 0,b eq 0) ? 42 : 7;}",
     ((0,0,7),(0,1,42),(1,0,42),(1,1,7))),
    ("bool-xor-composition", "fnc main(a:int):int{return Xor(a eq 0,true) && a neq 2 ? 42 : 7;}",
     ((0,7),(1,42),(2,7))),
    ("user-xor-call", "fnc Xor(a:int,b:int):int{return a+b;} fnc main():int{return Xor(19,23);}",
     ((42,),)),
    ("no-args", "fnc main():int{return 42;}", ((42,),)),
)
# Exact shared Git blobs, including the final newline added by build().
GUARD_FIXTURE_BLOBS = {
    "guard-brace-continue": "fa9dd0394a3ef08f0ecb4ff010e94cc72a859126",
    "guard-brace-failure": "f4cdcada6b147c77a1f0f6ae6469a31ee44f2603",
    "guard-keyword-continue": "82a2afa93e827f387407d0530febf328254599dc",
    "guard-keyword-failure": "13f41e961534f8b93895c224b04df77feb286793",
    "guard-canonical-scalar": "e2344e360ac643e25df7ca14eb01432929cecbe5",
}
TRAPS = (
    ("division-zero", "fnc main(a:int,b:int):int{return a/b;}", (42,0), "i64 division by zero"),
    ("division-overflow", "fnc main(a:int,b:int):int{return a/b;}", (-9223372036854775808,-1), "i64 division overflow"),
    ("division-propagates", "fnc q(a:int,b:int):int{return a/b;} fnc wrap(a:int,b:int):int{return q(a,b)+9223372036854775807;} fnc main(a:int,b:int):int{return wrap(a,b);}", (42,0), "i64 division by zero"),
    ("division-left-error-first", "fnc main(a:int):int{return (1/a)+(9223372036854775807+1);}", (0,), "i64 division by zero"),
    ("division-left-overflow-first", "fnc main(a:int):int{return (9223372036854775807+a)+(1/(a-1));}", (1,), "i64 add overflow"),
    ("overflow-add", "fnc plus(n:int):int{return n+1;}"
     " fnc main(n:int):int{return plus(n);}",
     (9223372036854775807,), "i64 add overflow"),
    ("overflow-mul", "fnc mul(n:int):int{return n*3;}"
     " fnc main(n:int):int{return mul(n);}",
     (9223372036854775807,), "i64 mul overflow"),
    ("overflow-sub", "fnc main(n:int):int{return n-1;}",
     (-9223372036854775808,), "i64 sub overflow"),
    ("logical-and-evaluates-rhs", "fnc main(a:int):int{return a eq 1 && 9223372036854775807+a eq 0 ? 7 : 42;}",
     (1,), "i64 add overflow"),
    ("logical-or-evaluates-rhs", "fnc main(a:int):int{return a eq 0 || 9223372036854775807+a eq 0 ? 7 : 42;}",
     (1,), "i64 add overflow"),
    ("bool-xor-eager-rhs", "fnc main(a:int):int{return Xor(true,9223372036854775807+a eq 0) ? 42 : 7;}",
     (1,), "i64 add overflow"),
    ("bool-xor-eager-lhs", "fnc main(a:int):int{return Xor(9223372036854775807+a eq 0,false) ? 42 : 7;}",
     (1,), "i64 add overflow"),
    ("bool-xor-left-before-right", "fnc main(a:int):int{return Xor(9223372036854775807+a eq 0,9223372036854775807*a*2 eq 0) ? 42 : 7;}",
     (1,), "i64 add overflow"),
    ("unbounded-recursion", "fnc always(n:int):int{return always(n);}"
     " fnc main(n:int):int{return always(n);}",
     (1,), "call depth exceeded"),
)
INVALID = (
    ("division-bool-left", "fnc main():int{return true/2;}", "Boolean is not an int"),
    ("division-bool-right", "fnc main():int{return 2/false;}", "Boolean is not an int"),
    ("logical-int-left", "fnc main():int{return 1 && true ? 42 : 7;}", "condition_not_bool"),
    ("logical-int-right", "fnc main():int{return false && 1 ? 42 : 7;}", "condition_not_bool"),
    ("logical-or-int-right", "fnc main():int{return true || 1 ? 42 : 7;}", "condition_not_bool"),
    ("logical-bool-return", "fnc main():int{return true && false;}", "Boolean is not an int"),
    ("guard-int-condition", "fnc main():int{guard 1 {return 0;} return 42;}", "condition_not_bool"),
    ("guard-int-second-check", "fnc main():int{guard false, 1 else return 0; end return 42;}", "condition_not_bool"),
    ("guard-fallthrough", "fnc main():int{guard false {} return 42;}", "guard_failure_requires_return"),
    ("guard-break", "fnc main():int{guard false else break; end return 42;}", "guard_failure_requires_return"),
    ("guard-throw", "fnc main():int{guard false {throw 1;} return 42;}", "guard_failure_requires_return"),
    ("guard-void-return", "fnc main():int{guard false {return;} return 42;}", "expected callable expression"),
    ("guard-bool-return", "fnc main():int{guard false {return true;} return 42;}", "Boolean is not an int"),
    ("guard-missing-tail", "fnc main():int{guard true {return 1;}}", "unexpected token"),
    ("guard-wrong-closer", "fnc main():int{guard true else return 1; fi return 42;}", "unexpected token"),
    ("guard-reserved-formal", "fnc helper(guard:int):int{return 42;} fnc main():int{return 42;}", "reserved predicate word"),
    ("bool-xor-mixed", "fnc main():int{return Xor(true,1) ? 42 : 7;}", "Boolean Xor requires two bool operands"),
    ("bool-xor-arity", "fnc main():int{return Xor(true) ? 42 : 7;}", "Boolean Xor requires two bool operands"),
    ("bool-xor-shadow-function", "fnc main():int{return Xor(true,false) ? 42 : 7;} fnc Xor(a:int,b:int):int{return a+b;}", "Boolean Xor intrinsic is shadowed"),
    ("bool-xor-shadow-formal", "fnc main(Xor:int):int{return Xor(true,false) ? 42 : 7;}", "Boolean Xor intrinsic is shadowed"),
    ("bool-xor-symbolic", "fnc main():int{return true ^^ false ? 42 : 7;}", "FORGE_XOR_BACKEND_UNSUPPORTED"),
    ("integer-condition", "fnc main():int{return if 1 then 2 else 3;}", "condition_not_bool"),
    ("integer-ternary", "fnc main():int{return 1 ? 2 : 3;}", "condition_not_bool"),
    ("integer-unless", "fnc main():int{return unless 1 then 2 else 3 fi;}", "condition_not_bool"),
    ("bool-return", "fnc main():int{return true;}", "Boolean is not an int"),
    ("bool-arithmetic", "fnc main():int{return true+1;}", "Boolean is not an int"),
    ("bool-ordered", "fnc main():int{return true<false ? 2 : 3;}", "Boolean is not an int"),
    ("mixed-equality", "fnc main():int{return true eq 1 ? 2 : 3;}", "comparison operand type mismatch"),
    ("bool-arg", "fnc f(n:int):int{return n;} fnc main():int{return f(true);}", "Boolean is not an int"),
    ("bool-arm", "fnc main():int{return true ? false : 3;}", "Boolean is not an int"),
    ("negated-missing-fi", "fnc main():int{return unless true then 2 else 3;}", "unexpected token"),
    ("neq-boundary", "fnc main():int{return 1 !equation 2 ? 3 : 4;}", "invalid character"),
    ("unreachable-type-error", "fnc hidden():int{return true;} fnc main():int{return 42;}", "Boolean is not an int"),
    ("wrong-arity", "fnc f(n:int):int{return n;} fnc main():int{return f(1,2);}", "function arity mismatch"),
    ("unknown-target", "fnc main():int{return absent(1);}", "unknown function"),
    ("unreachable-invalid", "fnc bad():int{return absent();} fnc main():int{return 1;}", "unknown function"),
    ("duplicate-name", "fnc f():int{return 1;} fnc f():int{return 2;} fnc main():int{return 1;}", "duplicate function"),
    ("unknown-param", "fnc main():int{return oops;}", "unknown parameter"),
    ("unsupported-loop", "fnc main():int{return while 1 then 2;}", "unknown parameter"),
)
DRIVER = r"""#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int64_t oresforge_entry(PARAMS);
struct result_pair { int64_t value; uint64_t status; };
struct outcome { int64_t value; uint64_t tag; uint64_t effect; };
extern void oresforge_entry_effect(EFFECT_PARAMS);
extern struct result_pair oresforge_entry_checked(PARAMS);
int main(int argc,char **argv) {
 int checked = argc>1 && strcmp(argv[1],"--checked")==0;
 int effect = argc>1 && strcmp(argv[1],"--effect")==0;
 if (checked || effect) { --argc; ++argv; }
 if (argc!=COUNT+1)return 2;
 int64_t v[4]={0,0,0,0};
 (void)v; /* zero-argument native entries do not consume the array */
 for(int i=0;i<COUNT;i++){
  char *end=0;errno=0;
  const long long val=strtoll(argv[i+1],&end,10);
  if(errno||!*argv[i+1]||*end)return 3;
  v[i]=(int64_t)val;
 }
 if (effect) {
  struct outcome result={-1,UINT64_MAX,UINT64_MAX};
  oresforge_entry_effect(EFFECT_ARGS);
  for (int iteration=0; iteration<8; ++iteration) {
   struct outcome again={-1,UINT64_MAX,UINT64_MAX};
   oresforge_entry_effect(REPEAT_ARGS);
   if (again.value!=result.value || again.tag!=result.tag || again.effect!=result.effect) return 6;
  }
  if(printf("%" PRId64 " %" PRIu64 " %" PRIu64 "\n",result.value,result.tag,result.effect)<0)return 4;
  return fflush(stdout)==0?0:5;
 }
 if (checked) {
  const struct result_pair result=oresforge_entry_checked(ARGS);
  /* Repeated invocation in the same process must not retain error state. */
  for (int iteration=0; iteration<8; ++iteration) {
   const struct result_pair again=oresforge_entry_checked(ARGS);
   if (again.value!=result.value || again.status!=result.status) return 6;
  }
  if(printf("%" PRId64 " %" PRIu64 "\n",result.value,result.status)<0)return 4;
  return fflush(stdout)==0?0:5;
 }
 const int64_t result=oresforge_entry(ARGS);
 if(printf("%" PRId64 "\n",result)<0)return 4;
 return fflush(stdout)==0?0:5;
}
"""

def execute(args,cwd,timeout=10):
    # Do not create large core files for deliberate SIGILL depth/overflow tests.
    def limits():
        import resource
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    return subprocess.run(args,cwd=cwd,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE,timeout=timeout,check=False,
                          preexec_fn=limits)

def require(value,message):
    if not value:raise RuntimeError(message)

def build(compiler,cc,root,label,source,param_count,result_entry=False,option_entry=False):
    file=root/"entry.atto"
    file.write_text(source+"\n",encoding="utf8")
    emit=execute([compiler,"emit-call-asm","x86_64",str(file)],root)
    require(emit.returncode==0 and not emit.stderr,
            "emit failed "+label+": "+emit.stderr.decode(errors="replace"))
    asm=emit.stdout.decode("ascii")
    require("  call .Lores_fn_" in asm or "call ." not in asm,
            "native call syntax invalid in "+label)
    require(".Lores_depth:" in asm and ".Lores_return_effect:" in asm and ".Lores_return_variant:" in asm and
            len(asm)<5000000,"no bounded native call frame "+label)
    (root/"entry.s").write_text(asm,encoding="ascii")
    signature=", ".join("int64_t" for _ in range(param_count)) or "void"
    arguments=", ".join("v[%d]"%i for i in range(param_count))
    driver=DRIVER
    if result_entry or option_entry:
        symbol="oresforge_entry_option" if option_entry else "oresforge_entry_result"
        other="oresforge_entry_result" if option_entry else "oresforge_entry_option"
        require(".globl "+symbol+"\n" in asm and
                ".globl "+other+"\n" not in asm and
                ".globl oresforge_entry\n" not in asm and
                ".globl oresforge_entry_checked\n" not in asm,
                "tagged entry must not expose an incompatible ABI "+label)
        driver=driver.replace("extern int64_t oresforge_entry(PARAMS);","")
        driver=driver.replace("oresforge_entry_checked",symbol)
        driver=driver.replace("const int64_t result=oresforge_entry(ARGS);","const int64_t result=0;")
    effect_params="struct outcome *"+(", "+signature if param_count else "")
    suffix=", "+arguments if param_count else ""
    driver=driver.replace("EFFECT_PARAMS",effect_params).replace("EFFECT_ARGS","&result"+suffix).replace("REPEAT_ARGS","&again"+suffix)
    driver=driver.replace("PARAMS",signature).replace("COUNT",str(param_count)).replace("ARGS",arguments)
    if label in ("division-zero", "division-overflow"):
        require(not result_entry and not option_entry and param_count==2,
                "checked scalar recovery requires two i64 parameters")
        anchor = '  if(printf("%" PRId64 " %" PRIu64 "\\n",result.value,result.status)'
        require(driver.count(anchor) == 1, "checked ABI recovery anchor drift")
        driver = driver.replace(anchor, '''  const struct result_pair recovered=oresforge_entry_checked(84,2);
  if (recovered.value!=42 || recovered.status!=0) return 7;
  const struct result_pair failed_again=oresforge_entry_checked(v[0],v[1]);
  if (failed_again.value!=result.value || failed_again.status!=result.status) return 8;
''' + anchor, 1)
    (root/"driver.c").write_text(driver,encoding="ascii")
    executable=root/"native"
    link=execute([cc,"-std=c11","-Wall","-Wextra","-Werror",
                  str(root/"driver.c"),str(root/"entry.s"),"-o",str(executable)],root)
    require(link.returncode==0,"link failed "+label+": "+link.stderr.decode(errors="replace"))
    return file,executable,asm

def main():
    a=argparse.ArgumentParser()
    a.add_argument("--compiler",required=True)
    opts=a.parse_args()
    require(sys.platform.startswith("linux") and platform.machine().lower() in ("x86_64","amd64"),
            "native call proof requires Linux x86-64")
    compiler=str(Path(opts.compiler).resolve(strict=True))
    cc=shutil.which("cc")
    require(bool(cc),"C linker missing")
    positives=negatives=0
    with tempfile.TemporaryDirectory(prefix="oresforge-native-calls-") as tmp:
        root=Path(tmp)
        for label,kind,source,cases in EFFECT_CASES:
            count=len(cases[0])-3
            require(0<=count<=4 and all(len(row)==count+3 for row in cases),
                    "malformed effect vector arity "+label)
            file,exe,_=build(compiler,cc,root,label,source,count,
                             result_entry=kind=="result",option_entry=kind=="option")
            for row in cases:
                inp=row[:-3]
                expected=(" ".join(map(str,row[-3:]))+"\n").encode()
                host=execute([compiler,"run-effect-calls",str(file),*map(str,inp)],root)
                native=execute([str(exe),"--effect",*map(str,inp)],root)
                require((host.returncode,host.stdout,host.stderr)==(0,expected,b""),
                        "effect host mismatch "+label+"/"+repr(inp))
                require((native.returncode,native.stdout,native.stderr)==(0,expected,b""),
                        "effect native mismatch "+label+"/"+repr(inp))
                if row[-1]!=0 and kind!="int":
                    legacy=execute([str(exe),"--checked",*map(str,inp)],root)
                    require(legacy.returncode==-signal.SIGILL and not legacy.stdout,
                            "escaping effect must not become a tagged return "+label)
                positives+=1
        for label,source,cases in OPTION_CASES:
            count=len(cases[0])-2
            file,exe,_=build(compiler,cc,root,label,source,count,option_entry=True)
            for row in cases:
                inp=row[:-2]
                expected=(str(row[-2])+" "+str(row[-1])+"\n").encode()
                host=execute([compiler,"run-option-calls",str(file),*map(str,inp)],root)
                native=execute([str(exe),"--checked",*map(str,inp)],root)
                require((host.returncode,host.stdout,host.stderr)==(0,expected,b""),
                        "scalar Option host mismatch "+label)
                require((native.returncode,native.stdout,native.stderr)==(0,expected,b""),
                        "scalar Option native mismatch "+label)
                for mode in ("run-calls","run-result-calls"):
                    wrong=execute([compiler,mode,str(file),*map(str,inp)],root)
                    require(wrong.returncode==1 and not wrong.stdout,
                            "Option must not leak through wrong typed host API")
                positives+=1
        for label,source,diagnostic in OPTION_INVALID:
            file=root/"option-invalid.atto"
            file.write_text(source+"\n",encoding="utf8")
            failure=execute([compiler,"emit-call-asm","x86_64",str(file)],root)
            require(failure.returncode==1 and not failure.stdout and diagnostic.encode() in failure.stderr,
                    "invalid Option emitted assembly "+label+": "+failure.stderr.decode(errors="replace"))
            negatives+=1
        for label,source,cases in RESULT_CASES:
            count=len(cases[0])-2
            file,exe,_=build(compiler,cc,root,label,source,count,result_entry=True)
            for row in cases:
                inp=row[:-2]
                expected=(str(row[-2])+" "+str(row[-1])+"\n").encode()
                host=execute([compiler,"run-result-calls",str(file),*map(str,inp)],root)
                native=execute([str(exe),"--checked",*map(str,inp)],root)
                require((host.returncode,host.stdout,host.stderr)==(0,expected,b""),
                        "scalar trap host mismatch "+label)
                require((native.returncode,native.stdout,native.stderr)==(0,expected,b""),
                        "scalar trap native mismatch "+label)
                wrong=execute([compiler,"run-calls",str(file),*map(str,inp)],root)
                require(wrong.returncode==1 and not wrong.stdout and b"requires run-result-calls" in wrong.stderr,
                        "Result must not leak through scalar host API")
                positives+=1
        file,exe,_=build(compiler,cc,root,"trap-depth",
            "fnc recurse(n:int):int{return recurse(n);} fnc main(n:int):Result<int,Error>{return trap! recurse(n);}",
            1,result_entry=True)
        host=execute([compiler,"run-result-calls",str(file),"1"],root)
        native=execute([str(exe),"--checked","1"],root)
        require(host.returncode==1 and not host.stdout and b"call depth exceeded" in host.stderr,
                "trap must not swallow host depth fault")
        require(native.returncode==-signal.SIGILL and not native.stdout,
                "trap must not swallow native depth fault")
        negatives+=1
        for label,source,diagnostic in RESULT_INVALID:
            file=root/"result-invalid.atto"
            file.write_text(source+"\n",encoding="utf8")
            failure=execute([compiler,"emit-call-asm","x86_64",str(file)],root)
            require(failure.returncode==1 and not failure.stdout and diagnostic.encode() in failure.stderr,
                    "invalid Result emitted assembly "+label+": "+failure.stderr.decode(errors="replace"))
            negatives+=1
        for label,source,cases in POSITIVE:
            number=len(cases[0])-1
            file,exe,asm=build(compiler,cc,root,label,source,number)
            if label in GUARD_FIXTURE_BLOBS:
                data=file.read_bytes()
                blob=hashlib.sha1(b"blob "+str(len(data)).encode("ascii")+b"\0"+data).hexdigest()
                require(blob==GUARD_FIXTURE_BLOBS[label],"shared guard fixture drift "+label)
            if label in ("factorial","fibonacci","mutual","nested"):
                require(asm.count("  call .Lores_fn_")>=1,"missing real native call "+label)
            if label in ("factorial","lazy"):
                require("  je .Lores_else_" in asm,"missing native lazy branch "+label)
            for row in cases:
                inp=row[:-1]
                expected=(str(row[-1])+"\n").encode()
                checked=execute([compiler,"run-calls",str(file),*map(str,inp)],root)
                native=execute([str(exe),*map(str,inp)],root)
                require((checked.returncode,checked.stdout,checked.stderr)==(0,expected,b""),
                        "interpreter returned wrong result "+label+"/"+repr(inp))
                require((native.returncode,native.stdout,native.stderr)==(0,expected,b""),
                        "native call returned wrong result "+label+"/"+repr(inp))
                native_checked=execute([str(exe),"--checked",*map(str,inp)],root)
                require((native_checked.returncode,native_checked.stdout,native_checked.stderr)==
                        (0,(str(row[-1])+" 0\n").encode(),b""),
                        "checked ABI success mismatch "+label+"/"+repr(inp))
                positives+=1
        for label,source,inp,diagnostic in TRAPS:
            file,exe,_=build(compiler,cc,root,label,source,len(inp))
            checked=execute([compiler,"run-calls",str(file),*map(str,inp)],root)
            require(checked.returncode==1 and checked.stdout==b"" and
                    diagnostic.encode() in checked.stderr,"interpreter failed to reject "+label)
            native=execute([str(exe),*map(str,inp)],root)
            require(native.returncode==-signal.SIGILL and native.stdout==b"",
                    "native overflow/depth failed to trap "+label+": "+str(native.returncode))
            native_checked=execute([str(exe),"--checked",*map(str,inp)],root)
            if diagnostic=="call depth exceeded":
                require(native_checked.returncode==-signal.SIGILL and native_checked.stdout==b"",
                        "depth must remain a non-arithmetic terminal fault")
            else:
                code=65538 if diagnostic=="i64 division by zero" else 65537
                require((native_checked.returncode,native_checked.stdout,native_checked.stderr)==
                        (0,("0 "+str(code)+"\n").encode(),b""),
                        "checked ABI failure identity mismatch "+label)
            negatives+=1
        for label,source,diagnostic in INVALID:
            file=root/"bad.atto"
            file.write_text(source+"\n",encoding="utf8")
            for command in ((compiler,"run-calls",str(file)),(compiler,"emit-call-asm","x86_64",str(file))):
                failure=execute(command,root)
                require(failure.returncode==1 and failure.stdout==b"" and
                        diagnostic.encode() in failure.stderr,
                        "semantic invalid case admitted or leaked output "+label)
            negatives+=1
        file=root/"unsupported.ores"
        file.write_text("fnc main():int{return 42;}\n",encoding="ascii")
        unsupported=execute([compiler,"emit-call-asm","arm64",str(file)],root)
        require(unsupported.returncode==1 and unsupported.stdout==b"" and
                b"currently supports x86_64 only" in unsupported.stderr,
                "ARM64 must fail explicitly, not produce wrong assembly")
    print(json.dumps({"kind":"forge-real-native-function-call-execution",
                      "arch":"x86_64","positives":positives,"negative_controls":negatives,
                      "native_interfunction_calls":True,"runtime_lazy_branch":True,
                      "bounded_recursion":True,"arm64_execution":False,
                      "self_hosted":False},sort_keys=True))

if __name__=="__main__":
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired,UnicodeError) as error:
        print("Native call proof FAILED: "+str(error),file=sys.stderr)
        sys.exit(1)
