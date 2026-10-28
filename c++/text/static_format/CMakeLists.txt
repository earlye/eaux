eaux_header_library(eaux_text_static_format eaux::text::static_format)
target_link_libraries(eaux_text_static_format INTERFACE eaux::text::ascii eaux::text::unicode)

# Float conversion backend: to_chars (std::to_chars), fmt (links fmt), vendored (Dragonbox plus a
# port of fmt's fixed-precision digits), vendored_floff (Dragonbox plus floff).
set(EAUX_STATIC_FORMAT_FLOAT_BACKEND to_chars CACHE STRING "static_format float conversion backend")
set_property(CACHE EAUX_STATIC_FORMAT_FLOAT_BACKEND PROPERTY STRINGS to_chars fmt vendored vendored_floff)
if(NOT EAUX_STATIC_FORMAT_FLOAT_BACKEND MATCHES "^(to_chars|fmt|vendored|vendored_floff)$")
  message(FATAL_ERROR "EAUX_STATIC_FORMAT_FLOAT_BACKEND: unknown backend ${EAUX_STATIC_FORMAT_FLOAT_BACKEND}")
endif()
if(EAUX_STATIC_FORMAT_FLOAT_BACKEND STREQUAL "fmt" OR EAUX_BUILD_BENCH)
  include(FetchContent)
  set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(fmt
    URL https://github.com/fmtlib/fmt/archive/refs/tags/12.2.0.tar.gz
    URL_HASH SHA256=8b852bb5aa6e7d8564f9e81394055395dd1d1936d38dfd3a17792a02bebd7af0
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SYSTEM)
  FetchContent_MakeAvailable(fmt)
endif()
if(NOT EAUX_STATIC_FORMAT_FLOAT_BACKEND STREQUAL "to_chars")
  string(TOUPPER ${EAUX_STATIC_FORMAT_FLOAT_BACKEND} backend)
  target_compile_definitions(eaux_text_static_format INTERFACE EAUX_STATIC_FORMAT_FLOAT_BACKEND_${backend})
endif()
if(EAUX_STATIC_FORMAT_FLOAT_BACKEND STREQUAL "fmt")
  target_link_libraries(eaux_text_static_format INTERFACE fmt::fmt)
endif()

if(EAUX_BUILD_TESTS)
  eaux_doctest(eaux_text_static_format_tests
    SOURCES
      tests/parse_test.cpp
      tests/printf_test.cpp
      tests/brace_test.cpp
      tests/text_test.cpp
      tests/user_type_test.cpp
      tests/output_test.cpp
    LIBRARIES eaux::text::static_format)
  # Depth limits below the stress test's 64 arguments and 129 segments: recursing per argument
  # or per segment stops it compiling. MSVC has no template-depth switch, so there only the
  # constexpr limit applies.
  eaux_doctest(eaux_text_static_format_stress
    SOURCES tests/stress_test.cpp
    LIBRARIES eaux::text::static_format)
  if(MSVC)
    target_compile_options(eaux_text_static_format_stress PRIVATE /constexpr:depth40)
  else()
    target_compile_options(eaux_text_static_format_stress PRIVATE -ftemplate-depth=40 -fconstexpr-depth=40)
  endif()
  eaux_compile_fail_tests(eaux_text_static_format ${CMAKE_CURRENT_SOURCE_DIR}/tests/fail
    LIBRARIES eaux::text::static_format)
endif()

if(EAUX_BUILD_BENCH)
  add_subdirectory(bench)
endif()
