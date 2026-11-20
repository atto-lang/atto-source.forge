# Execute the real Forge compiler, host assembler/linker and native guest.
# No Python and no static stub assembly: the compiler owns the emitted code.
foreach(required ORESFORGE_COMPILER ORESFORGE_CXX ROOT OUTPUT_DIR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "missing ${required}")
  endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(ASM "${OUTPUT_DIR}/tenant_entry.s")
set(EXE "${OUTPUT_DIR}/tenant_guest_test")
execute_process(
  COMMAND "${ORESFORGE_COMPILER}" emit-runtime-asm x86_64 "${ROOT}/verification/tenant_scalar_entry.ores"
  RESULT_VARIABLE emit_code
  OUTPUT_FILE "${ASM}"
  ERROR_VARIABLE diagnostics
)
if(NOT emit_code EQUAL 0)
  message(FATAL_ERROR "real Forge scalar compiler rejected guest: ${diagnostics}")
endif()
file(READ "${ASM}" text)
if(NOT text MATCHES "oresforge_entry:" OR NOT text MATCHES "addq")
  message(FATAL_ERROR "no actual native guest add instruction emitted")
endif()
execute_process(
  COMMAND "${ORESFORGE_CXX}" -std=c++20 -pthread "-I${ROOT}"
          "${ROOT}/verification/native_isolate_scalar_guest.cpp" "${ASM}" -o "${EXE}"
  RESULT_VARIABLE build_code OUTPUT_VARIABLE build_out ERROR_VARIABLE build_err
)
if(NOT build_code EQUAL 0)
  message(FATAL_ERROR "failed compiling/linking real Forge machine-code guest: ${build_out} ${build_err}")
endif()
execute_process(COMMAND "${EXE}" RESULT_VARIABLE run_code
                OUTPUT_VARIABLE run_out ERROR_VARIABLE run_err)
if(NOT run_code EQUAL 0)
  message(FATAL_ERROR "Forge tenant-native guest failed: ${run_code}: ${run_out} ${run_err}")
endif()
message(STATUS "Forge generated native tenant guest executed with independent state")
