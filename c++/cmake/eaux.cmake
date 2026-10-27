# Shared helpers for the eaux C++ packages.

# Flags for code this build compiles itself (tests, benchmarks), not for consumers.
function(eaux_target_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /bigobj)
    if(EAUX_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    if(EAUX_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# Header-only package: eaux_header_library(<target> <alias>) over ./include.
function(eaux_header_library target alias)
  add_library(${target} INTERFACE)
  add_library(${alias} ALIAS ${target})
  target_include_directories(${target} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_features(${target} INTERFACE cxx_std_23)
  # The packages treat all text, including string literals, as UTF-8.
  target_compile_options(${target} INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endfunction()

# eaux_doctest(<name> SOURCES ... LIBRARIES ...): a doctest executable registered with CTest.
function(eaux_doctest name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES;DEFINITIONS")
  add_executable(${name} ${arg_SOURCES})
  target_link_libraries(${name} PRIVATE ${arg_LIBRARIES} doctest::doctest_with_main)
  target_compile_definitions(${name} PRIVATE ${arg_DEFINITIONS})
  eaux_target_warnings(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# eaux_compile_fail_tests(<prefix> <dir> LIBRARIES ...): every <dir>/*.cpp must fail to compile
# with output matching the regex on its `// expect: <regex>` line. Each file is an
# EXCLUDE_FROM_ALL object library that its CTest case builds; the build tool can't run
# concurrently in one build tree, hence the resource lock.
function(eaux_compile_fail_tests prefix dir)
  cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "LIBRARIES")
  file(GLOB sources CONFIGURE_DEPENDS ${dir}/*.cpp)
  foreach(source IN LISTS sources)
    get_filename_component(stem ${source} NAME_WE)
    file(STRINGS ${source} expect REGEX "^// expect: " LIMIT_COUNT 1)
    if(NOT expect)
      message(FATAL_ERROR "${source}: missing `// expect: <regex>` line")
    endif()
    string(REGEX REPLACE "^// expect: " "" expect "${expect}")
    set(target ${prefix}_fail_${stem})
    add_library(${target} OBJECT EXCLUDE_FROM_ALL ${source})
    target_link_libraries(${target} PRIVATE ${arg_LIBRARIES})
    add_test(NAME ${prefix}.fail.${stem}
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target} --config $<CONFIG>)
    set_tests_properties(${prefix}.fail.${stem} PROPERTIES
      PASS_REGULAR_EXPRESSION "${expect}"
      RESOURCE_LOCK eaux_build_tree
      LABELS compile-fail)
  endforeach()
endfunction()
