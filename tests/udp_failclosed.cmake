# Execute the real Forge stage-0 compiler in check-only mode. Native UDP
# socket lowering is not implemented: policy source-model passing does not
# authorize emitting a fake guest success or mutating outputs.
if(NOT DEFINED ROOT OR NOT DEFINED FORGE OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "UDP compiler admission test requires ROOT/FORGE/OUT_DIR")
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")
# Positive scalar control prevents a compiler that rejects everything from
# being recorded as a correct unsupported-feature gate.
set(control "${OUT_DIR}/supported-control.atto")
file(WRITE "${control}" "fnc main(): int { return 42; }\n")
execute_process(COMMAND "${FORGE}" --check "${control}"
  RESULT_VARIABLE ok_status OUTPUT_VARIABLE ok_out ERROR_VARIABLE ok_err)
if(NOT "${ok_status}" STREQUAL "0" OR NOT "${ok_out}" STREQUAL ""
    OR NOT "${ok_err}" STREQUAL "")
  message(FATAL_ERROR
    "Forge positive scalar control failed before UDP negatives: status=${ok_status} err='${ok_err}'")
endif()

set(guest "${OUT_DIR}/unsupported-udp.atto")
set(sentinel "${OUT_DIR}/preserve-output.cpp")
file(WRITE "${guest}" "fnc main(): int { return network.bind_udp(0); }\n")
file(WRITE "${sentinel}" "untouched UDP admission sentinel\n")

foreach(input IN ITEMS "${guest}" "${ROOT}/stdlib/net/udp.atto")
  execute_process(COMMAND "${FORGE}" --check "${input}"
    RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
  if(NOT "${result}" STREQUAL "2" OR NOT "${stdout}" STREQUAL ""
      OR "${stderr}" STREQUAL "")
    message(FATAL_ERROR
      "Forge stage-0 failed closed UDP admission: status=${result} out='${stdout}' err='${stderr}'")
  endif()
endforeach()

file(READ "${sentinel}" after)
if(NOT "${after}" STREQUAL "untouched UDP admission sentinel\n")
  message(FATAL_ERROR "unsupported UDP check modified preexisting output")
endif()
