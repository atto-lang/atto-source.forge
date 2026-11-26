# Experimental stage-0 native scalar actor witness

This opt-in test profile forwards the legacy scalar emitter and serial actor ABI from #169/#178. It is not a production frontend or a declaration of current Atto syntax parity. The default compiler, build, and scheduler paths are unchanged.

```sh
python3 verification/scalar_actor_stage0_native.py --cc gcc --cxx g++
python3 verification/scalar_actor_stage0_native.py --cc clang --cxx clang++ --sanitize
```

Alternatively configure with `-DORESFORGE_EXPERIMENTAL_STAGE0_NATIVE_TESTS=ON` and run `ctest -R forge_stage0_compiled_native_actor --output-on-failure`. Linux GCC/Clang only. The test builds the narrow emitter, generates C++ from two source programs, compiles and links native guest binaries with the actual scalar heap and mailbox runtime, executes both, and checks ten denied source inputs. The emitter is not linked into the guests. All temporary binaries are removed after the run.

The admitted grammar is a legacy single mutable i64 field and one serial receive callback with checked addition or subtraction. It is not the current sealed-state grammar; no general source ownership, dynamic object allocation, recursive calls, await/select, or compiler-generated continuation support is claimed. The raw C heap ABI aborts on invariant violation or resource exhaustion; only the tested checked arithmetic failure is actor-local. Heap storage is reclaimed at owner destruction after quiescence; no secure-erasure or process-wide quota certification is claimed. Host C++ exceptions, scheduler metadata and construction may allocate. No allocation audit, tracing-GC absence audit of the full executable, hard real-time guarantee, Windows build, or strict native AOT certification is implied.

This test is a migration witness toward the canonical source contract, not authorization to enable this grammar in the default compiler.
