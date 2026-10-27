#include <eaux/text/static_format/static_format.hpp>

#include <doctest/doctest.h>

#include <climits>
#include <cstdio>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>

using namespace eaux::text::static_format::literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

// The same string through _bfmt and through std::format.
namespace {

std::string c_printf(char const* format, double v1, double v2) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  std::string out(static_cast<std::size_t>(std::snprintf(nullptr, 0, format, v1, v2)), '\0');
  std::snprintf(out.data(), out.size() + 1, format, v1, v2);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  return out;
}

}  // namespace

#define CHECK_BRACE(fmt, ...) CHECK(fmt##_bfmt(__VA_ARGS__) == std::format(fmt, __VA_ARGS__))

TEST_CASE("escapes and indexing") {
  CHECK("{{}}"_bfmt() == "{}");
  CHECK("{{{}}}"_bfmt(1) == "{1}");
  CHECK("{1} {0} {1}"_bfmt("a", "b") == "b a b");
  CHECK("{} {}"_bfmt(1, 2, 3) == "1 2");  // as in std::format, extra arguments are allowed
}

TEST_CASE("integers match std::format") {
  CHECK_BRACE("{}|{:d}|{:5}|{:<5}|{:^5}|{:>5}|{:*^7}|{:05}|{:+}|{: }", 42, 42, 42, 42, 42, 42, 42, 42, 42, 42);
  CHECK_BRACE("{}|{:5}|{:<5}|{:05}|{:+}|{: }", -42, -42, -42, -42, -42, -42);
  CHECK_BRACE("{:x}|{:X}|{:#x}|{:#X}|{:#010x}|{:o}|{:#o}|{:#o}|{:b}|{:#b}|{:#B}|{:+x}", 255, 255, 255, 255, 255, 255, 255, 0,
              5, 5, 5, 255);
  CHECK_BRACE("{:x}|{:#x}|{:08b}", -255, -255, -5);
  CHECK_BRACE("{}|{}|{}|{}", INT_MIN, INT_MAX, LLONG_MIN, ULLONG_MAX);
  CHECK_BRACE("{}|{}|{:x}", static_cast<signed char>(-5), static_cast<unsigned char>(200), std::uint8_t{255});
  CHECK_BRACE("{:<05}|{:>06}|{:#>06x}", 42, 42, 255);
  CHECK_BRACE("{:c}|{:-}|{:-}", 65, 5, -5);
}

// std::format throws for a {:c} value that does not fit in char; _bfmt writes the code point as
// UTF-8 and substitutes U+FFFD for anything that is not a Unicode scalar value.
TEST_CASE("{:c} on an integer writes a code point") {
  CHECK("{:c}"_bfmt(65) == "A");
  // On an integer, sign, # and 0 are accepted and leave the character unchanged; c pads like text.
  CHECK("{:+c}|{:-c}|{: c}|{:#c}|{:05c}"_bfmt(65, 65, 65, 65, 65) == "A|A|A|A|A    ");
  CHECK("{:05c}"_bfmt(0x4E2D) == "\u4E2D   ");
  CHECK("{:c}|[{:4c}]|[{:>4c}]"_bfmt(0x4E2D, 0x4E2D, 0x4E2D) == "\u4E2D|[\u4E2D  ]|[  \u4E2D]");
  CHECK("{:c}"_bfmt(300) == "\u012C");
  CHECK("{:c}"_bfmt(static_cast<unsigned char>(0xE9)) == "\u00E9");
  CHECK("{:c}|{:c}|{:c}"_bfmt(-1, 0xD800, 0x110000) == "\uFFFD|\uFFFD|\uFFFD");
  CHECK("{:c}"_bfmt(0x1'0000'0041LL) == "\uFFFD");  // would wrap to 'A' if narrowed first
}

TEST_CASE("bool and char match std::format") {
  CHECK_BRACE("{}|{:s}|{:6}|{:>6}|{:d}|{:#x}|{:5d}", true, false, true, true, true, true, false);
  CHECK_BRACE("{}|{:c}|{:3}|{:>3}|{:d}|{:x}|{:#b}|{:+d}", 'a', 'b', 'c', 'd', 'A', 'A', 'A', 'A');
}

TEST_CASE("floating point matches std::format") {
  for (double v : {0.0, -0.0, 1.0, -1.5, 3.14159265358979, 1e-7, 123456789.125, 1e16, 1e100, 0.1, 1.0 / 3, 2.5}) {
    CAPTURE(v);
    CHECK_BRACE("{}|{:10}|{:<10}|{:^10}|{:+}|{: }|{:010}|{:#}", v, v, v, v, v, v, v, v);
    CHECK_BRACE("{:f}|{:.3f}|{:10.3f}|{:F}|{:+.2f}|{:#.0f}|{:010.2f}", v, v, v, v, v, v, v);
    CHECK_BRACE("{:e}|{:.3e}|{:E}|{:#.0e}", v, v, v, v);
    CHECK_BRACE("{:g}|{:.3g}|{:G}|{:.0g}|{:.3}", v, v, v, v, v);
    // libc++'s std::format drops a significant digit under # with g (0.10000 for 0.1); C's %#g is the rule.
    CHECK("{:#g}|{:#.3g}"_bfmt(v, v) == c_printf("%#g|%#.3g", v, v));
    CHECK_BRACE("{:a}|{:.3a}|{:A}|{:#a}|{:12a}", v, v, v, v, v);
  }
  CHECK_BRACE("{}|{}|{:.3}", 2.5f, 0.1f, 0.1f);
  CHECK_BRACE("{}|{:e}", 1e-300, 5e-324);
}

TEST_CASE("infinity and NaN match std::format") {
  double const inf = std::numeric_limits<double>::infinity();
  double const nan = std::numeric_limits<double>::quiet_NaN();
  CHECK_BRACE("{}|{:F}|{:E}|{:08}|{:<8}|{:+}|{:a}", inf, inf, inf, inf, inf, inf, inf);
  CHECK_BRACE("{}|{:F}|{:08}|{:>8}", nan, nan, nan, nan);
  CHECK_BRACE("{}|{:08}", -inf, -inf);
}

TEST_CASE("strings and pointers match std::format") {
  CHECK_BRACE("{}|{:s}|{:8}|{:>8}|{:^8}|{:.2}|{:*<8.3}|{:.0}|", "hello", "hello"s, "hello"sv, "hello", "hello", "hello", "hello",
              "hello");
  char array[8] = "abc";
  CHECK("{}"_bfmt(array) == "abc");
  int x = 0;
  void const* p = &x;
  CHECK_BRACE("{}|{:p}|{:20}|{:>20}", p, p, p, p);
  CHECK_BRACE("{}|{:6}|", nullptr, nullptr);
}

TEST_CASE("unicode fill") {
  CHECK_BRACE("{:\u00B7>8}|{:\u4E2D^7}", 42, "ab");
}
