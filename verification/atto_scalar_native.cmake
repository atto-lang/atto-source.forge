cmake_minimum_required(VERSION 3.20)

# Runs the real Forge frontend and native emitted-assembly path, not a host oracle.
# This proves only the restricted stage-0 i64 subset, not general actor support.
foreach(required FORGE_COMPILER FORGE_CXX POSITIVE NEGATIVE OUTPUT_ROOT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "missing formal-scalar parameter ${required}")
  endif()
endforeach()
foreach(path FORGE_COMPILER FORGE_CXX POSITIVE NEGATIVE)
  if(NOT EXISTS "${${path}}")
    message(FATAL_ERROR "missing formal-scalar input: ${path}")
  endif()
endforeach()
file(SHA256 "${POSITIVE}" positive_digest)
file(SHA256 "${NEGATIVE}" negative_digest)
if(NOT positive_digest STREQUAL "08a415734e9992f47702dc9b1ccf2c5dceb74df05245eaa5fbb00512d18afc8a")
  message(FATAL_ERROR "positive .atto bytes differ from canonical five-backend fixture")
endif()
if(NOT negative_digest STREQUAL "e1f0ce88facd1fe58b0ce698c250302a5bae82c2aea5ae193b73641edf9105b7")
  message(FATAL_ERROR "negative .atto bytes differ from canonical rejection fixture")
endif()

execute_process(COMMAND "${FORGE_COMPILER}" run "${POSITIVE}"
  RESULT_VARIABLE interpreted_status OUTPUT_VARIABLE interpreted_output
  ERROR_VARIABLE interpreted_stderr TIMEOUT 15)
if(NOT "${interpreted_status}" STREQUAL "0" OR
   NOT "${interpreted_output}" STREQUAL "42\n" OR
   NOT "${interpreted_stderr}" STREQUAL "")
  message(FATAL_ERROR "Forge checked scalar frontend did not produce 42")
endif()

file(MAKE_DIRECTORY "${OUTPUT_ROOT}")
set(asm "${OUTPUT_ROOT}/formal-entry.s")
set(driver "${OUTPUT_ROOT}/formal-driver.cpp")
set(exe "${OUTPUT_ROOT}/formal-native")
file(REMOVE "${asm}" "${exe}")
execute_process(COMMAND "${FORGE_COMPILER}" emit-asm x86_64 "${POSITIVE}"
  RESULT_VARIABLE compile_status OUTPUT_FILE "${asm}"
  ERROR_VARIABLE compile_stderr TIMEOUT 15)
if(NOT "${compile_status}" STREQUAL "0" OR
   NOT "${compile_stderr}" STREQUAL "" OR NOT EXISTS "${asm}")
  message(FATAL_ERROR "Forge compiler failed to emit actual native assembly")
endif()
file(WRITE "${driver}" "#include <cstdio>\nextern \"C\" long long oresforge_entry(void);\nint main(){ if(std::printf(\"%lld\\n\",oresforge_entry()) < 0) return 1; return std::fflush(stdout)==0 ? 0 : 1; }\n")
execute_process(COMMAND "${FORGE_CXX}" -x assembler "${asm}" -x c++ "${driver}" -o "${exe}"
  RESULT_VARIABLE link_status OUTPUT_VARIABLE link_stdout
  ERROR_VARIABLE link_stderr TIMEOUT 30)
if(NOT "${link_status}" STREQUAL "0" OR NOT EXISTS "${exe}")
  message(FATAL_ERROR "Forge emitted assembly could not link into a native executable: ${link_stderr}")
endif()
execute_process(COMMAND "${exe}" RESULT_VARIABLE guest_status
  OUTPUT_VARIABLE guest_output ERROR_VARIABLE guest_stderr TIMEOUT 15)
if(NOT "${guest_status}" STREQUAL "0" OR
   NOT "${guest_output}" STREQUAL "42\n" OR
   NOT "${guest_stderr}" STREQUAL "")
  message(FATAL_ERROR "Forge emitted native guest did not produce 42")
endif()

foreach(mode IN ITEMS run ir emit-asm)
  if(mode STREQUAL "emit-asm")
    set(mode_args emit-asm x86_64 "${NEGATIVE}")
  else()
    set(mode_args "${mode}" "${NEGATIVE}")
  endif()
  execute_process(COMMAND "${FORGE_COMPILER}" ${mode_args}
    RESULT_VARIABLE negative_status OUTPUT_VARIABLE negative_stdout
    ERROR_VARIABLE negative_stderr TIMEOUT 15)
  string(FIND "${negative_stderr}" "expected integer" expected_diagnostic)
  if(NOT "${negative_status}" STREQUAL "1" OR
     NOT "${negative_stdout}" STREQUAL "" OR expected_diagnostic EQUAL -1)
    message(FATAL_ERROR "Forge ${mode} did not reject the undeclared source with a compiler diagnostic")
  endif()
endforeach()
message(STATUS "FORGE_REAL_NATIVE_SCALAR_GUEST_PASS: actual native executable, output 42, 3 compiler rejections; NO five-backend certification")
