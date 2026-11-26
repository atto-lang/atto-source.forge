# Forge `std/time` compiler admission

This backend owns its own `stdlib/std/time.atto` implementation and
is not required to share byte-identical source with the other Atto compilers.

The present stage-0 Forge C++ frontend admits pure scalar functions with
inlined calls; it cannot compile class-based `Duration`, `Instant`,
`ManualClock`, `Deadline`, `Timer` and `Interval`, or link
`import module time from "std/time"`. A file being present is not a
compiler/linker pass.

`tests/std_time_frontend_admission_test.cpp` exercises
`oresforge::parse_source` from the real production frontend with four
negative time cases plus a positive scalar `main():int` interpreted
as 42. It is registered with CTest and may be run directly:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -I. \
  compiler/frontend.cpp compiler/ir.cpp \
  tests/std_time_frontend_admission_test.cpp -o /tmp/atto-forge-time
/tmp/atto-forge-time
```

These checks prove unsupported syntax is rejected honestly. They do not
prove a Forge guest `std/time` artifact or scheduler-integrated
`sleep`, which require compiler class/Result support, trusted clocks,
timer queue and actor suspension lowering.
