# Executable portable signed-i64 conformance (Forge)

Consumes the exact reviewed 7-case source corpus (Git blob `bbc319c19ecd007109d33592e210091eb267e5a2`
and SHA-256 `39bddbe558ad1c336271f91e6403c4555f2f16a7ed33a3355ee897721b6ab02a`),
runs the actual built `oresforge run` CLI separately on all seven cases,
compares exact signed-i64 decimal output, and rejects five unsupported/overflow
sources without publishing output. All cases must execute, no skips.

The receipt `kind=oreslang-source-i64-executed-ir-interpreter` describes real
source parsing and IR interpreter evaluation. **It is NOT native assembly,
Native AOT, runtime actor, or cross-backend execution parity.** CI must
still run the CTest; a local static check cannot substitute for it.

After building: `python3 verification/source_i64_executable.py --compiler ./build/oresforge`
