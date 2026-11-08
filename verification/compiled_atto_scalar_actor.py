#!/usr/bin/env python3
"""Compile real tracked .atto actor source through oresforge -> C++ -> native actor.

This is an opt-in trusted C++ AOT backend bridge, not native Forge assembly,
Cranelift codegen, general actor language support, or self-hosting.
"""
import argparse
import hashlib
import json
import resource
from pathlib import Path
import subprocess
import sys
import tempfile

CASES = (
    ("actor-counter.atto", 0, False, (5, 3, 9, 7, 10)),
    ("actor-subtract.atto", 20, True, (15, 17, 11, 13, 10)),
    ("actor-overflow.atto", 9223372036854775807, False, ()),
    ("actor-underflow.atto", -9223372036854775808, True, ()),
)
DRIVER = r"""
#include "runtime/scalar_actor.hpp"
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
using oresforge::runtime::ScalarActor;
using oresforge::runtime::ActorFrameStack;
extern "C" void oresforge_actor_init(OresActorI64Heap*, std::uint64_t, std::uint64_t, ActorFrameStack&);
extern "C" std::int64_t oresforge_actor_handle(OresActorI64Heap*, std::uint64_t, std::uint64_t, std::int64_t, ActorFrameStack&);
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error("native guest actor assertion at line " + std::to_string(__LINE__)); } while (0)
int main() {
    ScalarActor first(101, 1, 8, oresforge_actor_init, oresforge_actor_handle);
    ScalarActor sibling(101, 1, 8, oresforge_actor_init, oresforge_actor_handle);
    const auto a = first.ref(), b = sibling.ref();
    CHECK(first.try_send(b, 3) == ScalarActor::Send::Foreign);
    CHECK(sibling.try_send(a, 3) == ScalarActor::Send::Foreign);
#if EXPECT_OVERFLOW
    CHECK(first.try_send(a, 1) == ScalarActor::Send::Accepted);
    bool caught = false;
    try { (void)first.run_one(); }
    catch (const std::overflow_error&) { caught = true; }
    CHECK(caught && first.done() && first.frame_stack().retired());
    CHECK(first.try_send(a, 2) == ScalarActor::Send::Closed);
    CHECK(first.run_one().status == ScalarActor::Turn::Stopped);
    CHECK(sibling.try_send(b, -1) == ScalarActor::Send::Accepted);
    CHECK(sibling.run_one().value == EXPECT_OVERFLOW_SIBLING);
    sibling.stop();
    CHECK(sibling.done());
#else
    CHECK(first.try_send(a, 5) == ScalarActor::Send::Accepted);
    CHECK(first.try_send(a, -2) == ScalarActor::Send::Accepted);
    CHECK(first.run_one().value == EXPECT_0);
    CHECK(first.run_one().value == EXPECT_1);
    CHECK(sibling.try_send(b, 9) == ScalarActor::Send::Accepted);
    CHECK(sibling.run_one().value == EXPECT_2);
    CHECK(first.try_send(a, 4) == ScalarActor::Send::Accepted);
    std::int64_t migrated = 0;
    std::thread carrier([&] { migrated = first.run_one().value; });
    carrier.join();
    CHECK(migrated == EXPECT_3);
    CHECK(!first.frame_stack().retired());
    first.stop();
    CHECK(first.done() && first.frame_stack().retired());
    CHECK(first.try_send(a, 1) == ScalarActor::Send::Closed);
    CHECK(sibling.try_send(b, 1) == ScalarActor::Send::Accepted);
    CHECK(sibling.run_one().value == EXPECT_4);
    sibling.stop();
    CHECK(sibling.done());
#endif
    std::puts("COMPILED_ATTO_ACTOR_PASS");
}
"""
# The mutation witness must exit 0 with an *observably wrong value*.
# An unrelated crash, sanitizer failure, or uncaught CHECK exception is never
# accepted as proof that the mutated actor's semantic behavior was detected.
PROBE = r"""
#include "runtime/scalar_actor.hpp"
#include <cstdint>
#include <cstdio>
using oresforge::runtime::ScalarActor;
using oresforge::runtime::ActorFrameStack;
extern "C" void oresforge_actor_init(OresActorI64Heap*, std::uint64_t, std::uint64_t, ActorFrameStack&);
extern "C" std::int64_t oresforge_actor_handle(OresActorI64Heap*, std::uint64_t, std::uint64_t, std::int64_t, ActorFrameStack&);
int main() {
  ScalarActor actor(7001, 1, 2, oresforge_actor_init, oresforge_actor_handle);
  if (actor.try_send(actor.ref(), 5) != ScalarActor::Send::Accepted) return 2;
  const auto result = actor.run_one();
  if (result.status != ScalarActor::Turn::Executed) return 3;
  if (std::printf("%lld\\n", static_cast<long long>(result.value)) <= 0) return 4;
  actor.stop();
  return actor.done() ? 0 : 5;
}
"""

def run(command, cwd, timeout=60):
    def disable_core_dumps():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    return subprocess.run(command, cwd=cwd, stdin=subprocess.DEVNULL,
                          capture_output=True, timeout=timeout, check=False,
                          preexec_fn=disable_core_dumps)

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def checked(command, cwd, label):
    result = run(command, cwd)
    require(result.returncode == 0, label + ": " +
            result.stderr.decode("utf-8", "replace")[:3000])
    return result

def mutate_source(source):
    # All of these must be denied by the same production frontend command.
    return (
        source.replace("define actor", "define class", 1),
        source.replace("let int", "int", 1),
        source.replace("-> int ::", "-> void ::", 1),
        source.replace("return ", "await return ", 1),
        source.replace("  end\nend", "  end\nend\nimport unsafe", 1),
        source.replace("  end\nend", "  end\nend\n" + source, 1),
        source.replace(" = ", " *= ", 1),
        source.replace("9223372036854775807", "9223372036854775808", 1)
              if "9223372036854775807" in source else source + "\x00",
        source + "\x00",
        source + "\n\u00e9",
        source + "\n#host-code",
        source + ("\n" + "x"*8192),
    )

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--sanitized", action="store_true")
    opt = parser.parse_args()
    root = Path(opt.root).resolve(strict=True)
    compiler = Path(opt.compiler).resolve(strict=True)
    native_flags = (["-O1", "-g", "-fsanitize=address,undefined",
                     "-fno-omit-frame-pointer"] if opt.sanitized else ["-O2", "-DNDEBUG"])
    hashes = []
    successes = denials = mutations = 0
    with tempfile.TemporaryDirectory(prefix="oresforge-atto-actors-") as dirname:
        temp = Path(dirname)
        heap_obj = temp / "actor-heap.o"
        checked([opt.cc, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                 *native_flags, "-I", str(root), "-c",
                 str(root/"runtime/arena/ActorOwnedI64Heap.c"), "-o", str(heap_obj)],
                temp, "compile native actor heap")
        for fixture, initial, subtraction, results in CASES:
            source = (root/"examples"/fixture).read_bytes()
            require(source and len(source) <= 8192 and b"\x00" not in source,
                    "invalid tracked .atto fixture: " + fixture)
            path = temp / "actor.atto"
            path.write_bytes(source)
            emitted = checked([str(compiler), "emit-actor-cxx", str(path)],
                              temp, "emit " + fixture)
            native = emitted.stdout.decode("ascii")
            require("ores_actor_i64_heap_field_read" in native and
                    "ores_actor_i64_heap_field_write" in native and
                    "frames.push" in native and "frames.pop" in native and
                    "frames.read" in native and
                    "throw std::overflow_error" in native,
                    "compiler did not emit native actor heap/frame ABI")
            generated = temp/"actor-generated.cpp"
            generated.write_text(native, encoding="ascii")
            values = list(results) + [0]*max(0, 5-len(results))
            driver = DRIVER.replace("EXPECT_OVERFLOW_SIBLING",
                                    str(initial + 1 if subtraction else initial - 1))
            driver = driver.replace("EXPECT_OVERFLOW", "1" if not results else "0")
            for i, v in enumerate(values[:5]):
                driver = driver.replace("EXPECT_" + str(i), str(v))
            (temp/"driver.cpp").write_text(driver, encoding="ascii")
            executable = temp/"actor-native"
            checked([opt.cxx, "-std=c++20", *native_flags, "-Wall", "-Wextra",
                     "-Wpedantic", "-Werror", "-UNDEBUG", "-pthread",
                     "-I", str(root), str(generated),
                     str(root/"runtime/scalar_actor.cpp"),
                     str(temp/"driver.cpp"), str(heap_obj), "-o", str(executable)],
                    temp, "link compiled native actor " + fixture)
            executed = checked([str(executable)], temp, "execute native actor " + fixture)
            require(executed.stdout == b"COMPILED_ATTO_ACTOR_PASS\n",
                    "unexpected compiled native actor output: " + fixture)
            successes += 1
            # Compiler-produced semantics must affect a *healthy native
            # execution*, not merely cause an unrelated sanitizer crash.
            binary_hash = hashlib.sha256(executable.read_bytes()).hexdigest()
            if results:
                expected = "old - argument" if subtraction else "old + argument"
                swapped = "old + argument" if subtraction else "old - argument"
                require(native.count(expected) == 1, "ambiguous emitted arithmetic")
                (temp/"probe.cpp").write_text(PROBE, encoding="ascii")
                def compile_probe(label):
                    checked([opt.cxx, "-std=c++20", *native_flags,
                             "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                             "-pthread", "-I", str(root),
                             str(generated), str(root/"runtime/scalar_actor.cpp"),
                             str(temp/"probe.cpp"), str(heap_obj), "-o", str(executable)],
                            temp, label)
                compile_probe("link native mutation baseline")
                baseline = checked([str(executable)], temp, "execute mutation baseline")
                require(baseline.stdout == (str(results[0]) + "\\n").encode("ascii"),
                        "mutation baseline result differs from source oracle")
                generated.write_text(native.replace(expected, swapped), encoding="ascii")
                compile_probe("link negative native mutation")
                changed = checked([str(executable)], temp, "execute negative native mutation")
                require(changed.stdout != baseline.stdout and changed.stdout.endswith(b"\\n"),
                        "arithmetic mutation did not change observable i64 result")
                mutations += 1
            # The default/legacy compiler modes must not gain actor syntax
            # by transitive admission from this opt-in source-to-C++ mode.
            for mode in (("run",), ("ir",), ("run-runtime",),
                         ("ir-runtime",), ("run-calls",),
                         ("emit-asm", "x86_64"), ("emit-runtime-asm", "x86_64"),
                         ("emit-call-asm", "x86_64")):
                rejected_mode = run([str(compiler), *mode, str(path)], temp)
                require(rejected_mode.returncode != 0 and rejected_mode.stdout == b"",
                        "legacy Forge CLI unexpectedly admitted actor declaration: " +
                        "/".join(mode))
                denials += 1
            for index, bad in enumerate(mutate_source(source.decode("ascii"))):
                path.write_bytes(bad.encode("utf-8"))
                denied = run([str(compiler), "emit-actor-cxx", str(path)], temp)
                require(denied.returncode != 0 and denied.stdout == b"" and
                        b"oresforge:" in denied.stderr,
                        "unsupported actor syntax leaked partial output: " +
                        fixture + ":" + str(index))
                denials += 1
            hashes.append({
                "fixture": fixture,
                "fixture_sha256": hashlib.sha256(source).hexdigest(),
                "generated_cpp_sha256": hashlib.sha256(native.encode("ascii")).hexdigest(),
                "native_binary_sha256": binary_hash,
                "source_checked": True,
                "compiled_native_executed": True
            })
    print(json.dumps({"classification": "compiler-source-to-C++-AOT-and-actor-host-runtime",
                      "self_hosted": False, "native_forge_asm": False,
                      "sanitized": opt.sanitized,
                      "positive_executable_fixtures": successes,
                      "negative_source_admission": denials,
                      "negative_native_mutations": mutations,
                      "fixtures": hashes}, sort_keys=True))

if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        print("Forge actor compilation test failed:", error, file=sys.stderr)
        sys.exit(1)
