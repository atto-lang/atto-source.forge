# Executed stage-0 .atto -> verified callable IR -> x86_64 assembly
# -> host assembler/linker -> native machine-code guest. No stub compiler.
# Use source-local CTest on an exact authorized PR SHA (no cross-org transfer).
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED ROOT OR NOT DEFINED VERIFIER OR NOT DEFINED CXX OR
   NOT DEFINED OUT_DIR OR NOT DEFINED HOST_ARCH)
  message(FATAL_ERROR "missing native callable verification inputs")
endif()
# cmake -P does not populate CMAKE_HOST_SYSTEM_PROCESSOR on every host:
# bind architecture from CMake configure through -DHOST_ARCH.
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR
   NOT HOST_ARCH MATCHES "^(x86_64|amd64|AMD64)$")
  message(FATAL_ERROR "the SysV x86_64 guest proof requires Linux x86_64")
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")

function(check_native_callable name must_trap)
  set(src "${ROOT}/tests/fixtures/static-dispatch/${name}.atto")
  set(asm_file "${OUT_DIR}/${name}.s")
  if(NOT EXISTS "${src}")
    message(FATAL_ERROR "${name}: missing canonical Atto source fixture")
  endif()
  if(NOT must_trap)
    execute_process(
      COMMAND "${VERIFIER}" "${src}" --check-42
      RESULT_VARIABLE check_rc OUTPUT_VARIABLE checked ERROR_VARIABLE check_err
      TIMEOUT 20)
    if(NOT "${check_rc}" STREQUAL "0" OR
       NOT "${checked}" MATCHES "FORGE_STAGE0_NATIVE_CALL_ORACLE[|]value=42")
      message(FATAL_ERROR "${name}: real interpreter/IR oracle failed: ${check_rc}: ${check_err}")
    endif()
  endif()
  if("${name}" STREQUAL "trap-step-budget")
    # The interpreter rejects the *same* branching source via its existing
    # bounded AST evaluation. The native guest must reject it by SIGILL too.
    execute_process(
      COMMAND "${VERIFIER}" "${src}" --check-42
      RESULT_VARIABLE fuel_rc OUTPUT_VARIABLE fuel_stdout
      ERROR_VARIABLE fuel_stderr TIMEOUT 20)
    if(NOT "${fuel_rc}" STREQUAL "1" OR
       NOT "${fuel_stdout}" STREQUAL "" OR
       NOT "${fuel_stderr}" MATCHES "callable execution budget exceeded")
      message(FATAL_ERROR
        "trap-step-budget: interpreter must reject exhausted fuel: rc=[${fuel_rc}] stderr=[${fuel_stderr}]")
    endif()
  endif()
  execute_process(
    COMMAND "${VERIFIER}" "${src}" --asm
    RESULT_VARIABLE asm_rc OUTPUT_VARIABLE asm_text ERROR_VARIABLE asm_err
    TIMEOUT 20)
  if(NOT "${asm_rc}" STREQUAL "0" OR
     NOT asm_text MATCHES "oresforge_entry:")
    message(FATAL_ERROR "${name}: canonical source did not emit native assembly: ${asm_rc}: ${asm_err}")
  endif()
  if(NOT asm_text MATCHES "call[ \t]+[.]Lores_fn_" OR
     asm_text MATCHES "(call|jmp)[ \t]+\\*")
    message(FATAL_ERROR "${name}: missing direct native call or indirect dispatch escaped")
  endif()
  # Two emissions must be byte-identical before any assembler is invoked.
  execute_process(
    COMMAND "${VERIFIER}" "${src}" --asm
    RESULT_VARIABLE asm_again_rc OUTPUT_VARIABLE asm_again ERROR_VARIABLE asm_again_err
    TIMEOUT 20)
  if(NOT "${asm_again_rc}" STREQUAL "0" OR
     NOT "${asm_text}" STREQUAL "${asm_again}")
    message(FATAL_ERROR "${name}: non-deterministic native assembly output")
  endif()
  file(WRITE "${asm_file}" "${asm_text}")

  foreach(opt IN ITEMS O0 O2)
    set(bin "${OUT_DIR}/${name}-${opt}.guest")
    set(driver "${OUT_DIR}/${name}-${opt}-driver.cpp")
    if(must_trap)
      file(WRITE "${driver}"
        "#include <cstdint>\nextern \"C\" std::int64_t oresforge_entry();\n"
        "int main(){volatile std::int64_t value=oresforge_entry();(void)value;return 99;}\n")
    else()
      file(WRITE "${driver}"
        "#include <cstdint>\nextern \"C\" std::int64_t oresforge_entry();\n"
        // Two independent entry invocations ensure the private native fuel
        // counter is reset on every call; the near-budget positive guest
        // would trap on the second entry if it reused leftover fuel.
        "int main(){const auto first=oresforge_entry();const auto second=oresforge_entry();return first==42 && second==42 ? 0 : 17;}\n")
    endif()
    execute_process(
      COMMAND "${CXX}" "-${opt}" -std=c++20 -Wall -Wextra -Werror -pedantic
        -no-pie "${asm_file}" "${driver}" -o "${bin}"
      RESULT_VARIABLE link_rc OUTPUT_VARIABLE link_out ERROR_VARIABLE link_err
      TIMEOUT 45)
    if(NOT "${link_rc}" STREQUAL "0" OR NOT EXISTS "${bin}")
      message(FATAL_ERROR "${name}/${opt}: native assembler/linker failed: ${link_rc}: ${link_err}")
    endif()
    if(must_trap)
      # Bash converts the x86 ud2/SIGILL signal into exit status 128+4=132.
      # 99 means the guest returned unexpectedly; other exits are not accepted.
      execute_process(
        COMMAND /bin/bash -c "ulimit -c 0; \"\$1\" >/dev/null 2>/dev/null" bash "${bin}"
        RESULT_VARIABLE guest_rc OUTPUT_VARIABLE guest_out ERROR_VARIABLE guest_err
        TIMEOUT 10)
      if(NOT "${guest_rc}" STREQUAL "132")
        message(FATAL_ERROR "${name}/${opt}: expected SIGILL(132), got [${guest_rc}]; stderr=[${guest_err}]")
      endif()
    else()
      execute_process(
        COMMAND "${bin}"
        RESULT_VARIABLE guest_rc OUTPUT_VARIABLE guest_out ERROR_VARIABLE guest_err
        TIMEOUT 10)
      if(NOT "${guest_rc}" STREQUAL "0" OR
         NOT "${guest_out}" STREQUAL "" OR NOT "${guest_err}" STREQUAL "")
        message(FATAL_ERROR "${name}/${opt}: native guest did not return 42; rc=[${guest_rc}] stderr=[${guest_err}]")
      endif()
    endif()
    file(SHA256 "${asm_file}" asm_sha256)
    message(STATUS "FORGE_STAGE0_NATIVE_EXECUTED|fixture=${name}|opt=${opt}|trap=${must_trap}|asm_sha256=${asm_sha256}")
  endforeach()
endfunction()

foreach(case IN ITEMS direct-42 direct-four-args direct-nested-calls
                      direct-lazy-branch direct-recursion-boundary
                      direct-i64-boundary direct-step-budget-boundary)
  check_native_callable("${case}" FALSE)
endforeach()
foreach(case IN ITEMS trap-overflow trap-depth-limit trap-step-budget)
  check_native_callable("${case}" TRUE)
endforeach()
message(STATUS "FORGE_STAGE0_NATIVE_AOT=20_guest_executions; positive=14; sigill=6")
