# Forge bootstrap has neither manifest-owned module linking nor once-only init.
# All refusal checks run before scalar IR/asm publication.
if(NOT DEFINED COMPILER OR NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "COMPILER and WORK_DIR are required")
endif()
file(MAKE_DIRECTORY "${WORK_DIR}")
function(reject_case mode label source diagnostic)
  set(path "${WORK_DIR}/${label}.ores")
  file(WRITE "${path}" "${source}")
  execute_process(COMMAND "${COMPILER}" "${mode}" "${path}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 15)
  if(status EQUAL 0 OR NOT stdout STREQUAL "")
    message(FATAL_ERROR "${label} admitted or emitted output")
  endif()
  string(FIND "${stderr}" "${diagnostic}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "${label} expected ${diagnostic}, got ${stderr}")
  endif()
endfunction()
reject_case(ir namespace-import
  "import namespace billing as payments from \"checkout\";\nfnc main():int{return 42;}\n"
  "verified manifest package graph")
reject_case(ir lifecycle-init
  "fnc init():int{return 1;}\nfnc main():int{return 42;}\n"
  "once-only reachable initialization lowering")
reject_case(run-calls lifecycle-native-calls
  "fnc init():int{return 1;}\nfnc main():int{return 42;}\n"
  "once-only reachable initialization lowering")

file(WRITE "${WORK_DIR}/unreferenced.ores"
  "fnc init():int{return 999;}\nfnc main():int{return 1;}\n")
file(WRITE "${WORK_DIR}/main.ores" "fnc main():int{return 42;}\n")
execute_process(COMMAND "${COMPILER}" run "${WORK_DIR}/main.ores"
  RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 15)
if(NOT status EQUAL 0 OR NOT stdout STREQUAL "42\n")
  message(FATAL_ERROR "unreferenced source altered scalar Forge run: ${stderr}")
endif()
