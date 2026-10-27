# Builds one compile-fail target and passes only if the build fails and its output matches every
# `// expect: <regex>` line of the source file.
#
#   cmake -DBUILD_DIR=<dir> -DTARGET=<target> -DCONFIG=<config> -DSOURCE=<file> -P check_compile_fail.cmake
file(STRINGS "${SOURCE}" expects REGEX "^// expect: ")
set(command "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target "${TARGET}")
if(CONFIG)
  list(APPEND command --config "${CONFIG}")
endif()
execute_process(COMMAND ${command} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(result EQUAL 0)
  message(FATAL_ERROR "${SOURCE} compiled, but it must not")
endif()
foreach(line IN LISTS expects)
  string(REGEX REPLACE "^// expect: " "" pattern "${line}")
  if(NOT output MATCHES "${pattern}")
    message(FATAL_ERROR "${SOURCE}: compiler output does not match `${pattern}`:\n${output}")
  endif()
endforeach()
