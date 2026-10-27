// Long format strings and many arguments. Parsing and expansion must not recurse per segment or
// per argument; if they regress to recursion these stop compiling within default limits.
#include <eaux/text/static_format/static_format.hpp>

#include <doctest/doctest.h>

#include <cstdio>
#include <format>
#include <string>

// The literal operators take the string as a template argument, so a macro that expands to
// several concatenated literals can be passed straight in.
#define PFMT(s) eaux::text::static_format::literals::operator""_pfmt<s>()
#define BFMT(s) eaux::text::static_format::literals::operator""_bfmt<s>()

// 64 lines of 64 bytes, one conversion each: a 4 KB format string taking 64 arguments.
#define LINE "row %02d: .....................................................\n"
#define LINES8 LINE LINE LINE LINE LINE LINE LINE LINE
#define LINES64 LINES8 LINES8 LINES8 LINES8 LINES8 LINES8 LINES8 LINES8

#define ARGS8(n) n + 0, n + 1, n + 2, n + 3, n + 4, n + 5, n + 6, n + 7
#define ARGS64 ARGS8(0), ARGS8(8), ARGS8(16), ARGS8(24), ARGS8(32), ARGS8(40), ARGS8(48), ARGS8(56)

static_assert(sizeof(LINES64) - 1 == 4096);

TEST_CASE("a 4 KB printf format string with 64 arguments") {
  std::string const formatted = PFMT(LINES64)(ARGS64);
  char expected[8192];
  int const n = std::snprintf(expected, sizeof expected, LINES64, ARGS64);
  CHECK(formatted == std::string(expected, static_cast<std::size_t>(n)));
}

// A table row: 64 fields of mixed kinds.
#define FIELDS8 "{:>10.4f}|{:<6}|{:4d}|{:x}|{:>10.4f}|{:<6}|{:4d}|{:x}|"
#define FIELDS64 FIELDS8 FIELDS8 FIELDS8 FIELDS8 FIELDS8 FIELDS8 FIELDS8 FIELDS8
#define VALUES8 1.5, "ab", 3, 255u, -2.25, "\u03C3", -7, 16u
#define VALUES64 VALUES8, VALUES8, VALUES8, VALUES8, VALUES8, VALUES8, VALUES8, VALUES8

TEST_CASE("a brace format string with 64 arguments") {
  CHECK(BFMT(FIELDS64)(VALUES64) == std::format(FIELDS64, VALUES64));
}
