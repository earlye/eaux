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
      tests/stress_test.cpp
    LIBRARIES eaux::text::static_format)
  eaux_compile_fail_tests(eaux_text_static_format ${CMAKE_CURRENT_SOURCE_DIR}/tests/fail
    LIBRARIES eaux::text::static_format)
endif()

if(EAUX_BUILD_BENCH)
  add_subdirectory(bench)
endif()
