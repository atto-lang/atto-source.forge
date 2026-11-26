# AttoLang `.atto` source admission — Forge bootstrap

Forge currently accepts a bounded single source-file path and passes its bytes to the same parser regardless of the filename extension. The CLI and documentation now recognize `.atto` as the canonical AttoLang source suffix, while retaining `.ores` compatibility.

CTest executes `examples/hello.atto` through `oresforge run` and checks the result `14`; a separate `.ores` case preserves the legacy path.

**Scope limitation:** this is single-file stage-0 source-path admission only. Forge still rejects all source imports before IR publication. It does not establish module resolution, `.NET`/LLVM/Java parity, Wasm output parity, or production compiler status. The executable subset remains restricted to the syntax already admitted by the Forge frontend, and the 1 MiB bounded regular-file reader remains unchanged.

Do not infer import parity from the `.atto` file suffix. See [Import parity v1](IMPORT_PARITY_V1.md) for the outstanding B/C link and runtime gates.