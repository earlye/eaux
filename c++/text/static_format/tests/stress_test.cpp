// Long format strings and many arguments. CMakeLists.txt builds this file with template and
// constexpr depth limits of 40, below both its 64 arguments and its 129 segments, so parsing or
// expansion that recursed per segment or per argument would stop it compiling. The expected
// output is spelled out rather than produced by std::format or snprintf, whose own depth needs
// would otherwise set the floor.
#include <eaux/text/static_format/static_format.hpp>

#include <doctest/doctest.h>

#include <string>

// The literal operators take the string as a template argument, so a macro that expands to
// several concatenated literals can be passed straight in.
#define PFMT(s) eaux::text::static_format::literals::operator""_pfmt<s>()
#define BFMT(s) eaux::text::static_format::literals::operator""_bfmt<s>()

#define DOTS "....................................................."

// 64 lines of 64 bytes, one conversion each: a 4 KB format string taking 64 arguments.
#define LINE "row %02d: " DOTS "\n"
#define LINES8 LINE LINE LINE LINE LINE LINE LINE LINE
#define LINES64 LINES8 LINES8 LINES8 LINES8 LINES8 LINES8 LINES8 LINES8

#define ARGS8(n) n + 0, n + 1, n + 2, n + 3, n + 4, n + 5, n + 6, n + 7
#define ARGS64 ARGS8(0), ARGS8(8), ARGS8(16), ARGS8(24), ARGS8(32), ARGS8(40), ARGS8(48), ARGS8(56)

static_assert(sizeof(LINES64) - 1 == 4096);

TEST_CASE("a 4 KB printf format string with 64 arguments") {
  std::string expected;
  for (int i = 0; i < 64; ++i) {
    expected += "row ";
    expected += static_cast<char>('0' + i / 10);
    expected += static_cast<char>('0' + i % 10);
    expected += ": " DOTS "\n";
  }
  CHECK(PFMT(LINES64)(ARGS64) == expected);
}

// A table row: 64 fields of mixed kinds.
#define FIELDS8 "{:>10.4f}|{:<6}|{:4d}|{:x}|{:>10.4f}|{:<6}|{:4d}|{:x}|"
#define FIELDS64 FIELDS8 FIELDS8 FIELDS8 FIELDS8 FIELDS8 FIELDS8 FIELDS8 FIELDS8
#define VALUES8 1.5, "ab", 3, 255u, -2.25, "σ", -7, 16u
#define VALUES64 VALUES8, VALUES8, VALUES8, VALUES8, VALUES8, VALUES8, VALUES8, VALUES8

TEST_CASE("a brace format string with 64 arguments") {
  std::string expected;
  for (int i = 0; i < 8; ++i) expected += "    1.5000|ab    |   3|ff|   -2.2500|σ     |  -7|10|";
  CHECK(BFMT(FIELDS64)(VALUES64) == expected);
}
