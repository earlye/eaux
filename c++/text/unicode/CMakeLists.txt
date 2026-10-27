eaux_header_library(eaux_text_unicode eaux::text::unicode)

if(EAUX_BUILD_TESTS)
  eaux_doctest(eaux_text_unicode_tests
    SOURCES tests/unicode_test.cpp tests/grapheme_break_test.cpp
    LIBRARIES eaux::text::unicode
    DEFINITIONS EAUX_GRAPHEME_BREAK_TEST="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/GraphemeBreakTest.txt")
endif()
