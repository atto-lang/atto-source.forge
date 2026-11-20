# Common advisory static facts v0 — Forge C++ bootstrap

`python3 verification/static_facts_v0.py --compiler /absolute/path/to/oresforge
--source /absolute/path/to/file.ores` admits **only** one literal-returning
`main():int` in a strict ASCII, comment-free source grammar, stages exact
source bytes privately and invokes the **actual** Forge compiler
`oresforge ir <staged-source>`. The compiler must exit successfully and emit
recognizable, bounded verified scalar IR before any JSON is published.

The source gate is deliberately narrower than Forge's arithmetic parser,
and must not be broadened using regexes without a real typed effect export.
No general loops, calls, actors, tasks, callback timing, ownership or
security facts are claimed. Output is `ores-static-facts/v0` with a single
`__root__.main/0`, empty loops/edges, and source SHA256 (identity, not
producer attestation).

CTest runs the negative grammar selftest and real compiler positive gate.
`ctest --test-dir build -R oresforge_static_facts_v0 --output-on-failure`
is required before merge. This does NOT constitute Native AOT or
hard-real-time work-bound certification.
