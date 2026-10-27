eaux_header_library(eaux_text_ascii eaux::text::ascii)

if(EAUX_BUILD_TESTS)
  eaux_doctest(eaux_text_ascii_tests
    SOURCES tests/ascii_string_view_test.cpp
    LIBRARIES eaux::text::ascii)
  eaux_compile_fail_tests(eaux_text_ascii ${CMAKE_CURRENT_SOURCE_DIR}/tests/fail
    LIBRARIES eaux::text::ascii)
endif()
