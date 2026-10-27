eaux_header_library(eaux_text_static_format eaux::text::static_format)
target_link_libraries(eaux_text_static_format INTERFACE eaux::text::ascii eaux::text::unicode)

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
