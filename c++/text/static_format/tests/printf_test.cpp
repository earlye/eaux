#include <eaux/text/static_format/static_format.hpp>

#include <doctest/doctest.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <string>
#include <string_view>

using namespace eaux::text::static_format::literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

template <class... Args>
std::string c_printf(char const* format, Args... args) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  std::string out(static_cast<std::size_t>(std::snprintf(nullptr, 0, format, args...)), '\0');
  std::snprintf(out.data(), out.size() + 1, format, args...);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  return out;
}

}  // namespace

// The same string through _pfmt and through the C library.
#define CHECK_PRINTF(fmt, ...) CHECK(fmt##_pfmt(__VA_ARGS__) == c_printf(fmt, __VA_ARGS__))

TEST_CASE("the original formattype tests") {
  CHECK("test"_pfmt() == "test");
  CHECK("test %s"_pfmt("this") == "test this");
  CHECK("test %s and %s"_pfmt("this", "that") == "test this and that");
  CHECK("test %d"_pfmt(2) == "test 2");
  CHECK("test %3.1f"_pfmt(3.1) == "test 3.1");
  CHECK("%0.1f test"_pfmt(3.1) == "3.1 test");
  CHECK("test %2$s and %1$s"_pfmt("this", "that") == "test that and this");
  CHECK("test %%"_pfmt() == "test %");
  CHECK("0x%02X"_pfmt(0xff) == "0xFF");
  CHECK("0x%02x"_pfmt(0xFF) == "0xff");
  CHECK("0x%04x"_pfmt(0xFF) == "0x00ff");
  CHECK("%d"_pfmt(2000) == "2000");
  CHECK("%d-%02d"_pfmt(2000, 1) == "2000-01");
  CHECK("%d-%02d-%02dT"_pfmt(2000, 1, 2) == "2000-01-02T");
  CHECK("%d-%02d-%02dT%02d:%02d:%02d.%06dZ"_pfmt(2000, 1, 2, 12, 35, 4, 123456) == "2000-01-02T12:35:04.123456Z");
  CHECK("unrecognized visitor type:{%1%} concrete visitor type:{%2%} host:{%3%}"_pfmt("a"s, "b"s, "c"s) ==
        "unrecognized visitor type:{a} concrete visitor type:{b} host:{c}");
}

TEST_CASE("integers match the C library") {
  CHECK_PRINTF("%d|%i|%5d|%-5d|%05d|%+d|% d|%+05d", 42, 42, 42, 42, 42, 42, 42, 42);
  CHECK_PRINTF("%d|%5d|%-5d|%05d|%+d|% d|%+05d", -42, -42, -42, -42, -42, -42, -42);
  CHECK_PRINTF("%.3d|%8.3d|%-8.3d|%08.3d|%.0d|%.0d|%5.0d", 7, 7, 7, 7, 0, 3, 0);
  CHECK_PRINTF("%d|%d", INT_MIN, INT_MAX);
  CHECK_PRINTF("%lld|%lld", LLONG_MIN, LLONG_MAX);
  CHECK_PRINTF("%u|%llu", UINT_MAX, ULLONG_MAX);
  CHECK_PRINTF("%x|%X|%#x|%#X|%#x|%08x|%#08x|%-#8x|%.4x|%#.4x", 255u, 255u, 255u, 255u, 0u, 255u, 255u, 255u, 255u, 255u);
  CHECK_PRINTF("%o|%#o|%#o|%#.3o|%#5o", 8u, 8u, 0u, 8u, 0u);
  CHECK_PRINTF("%+u|% u|%+x", 5u, 5u, 5u);
  CHECK_PRINTF("%hhd|%hd|%ld|%zu|%jd|%td", 1, 2, 3L, std::size_t{4}, std::intmax_t{5}, std::ptrdiff_t{6});
}

TEST_CASE("unsigned conversions reinterpret negative values as C does") {
  CHECK("%u"_pfmt(-1) == c_printf("%u", static_cast<unsigned>(-1)));
  CHECK("%x"_pfmt(-1) == "ffffffff");
  CHECK("%x"_pfmt(std::int64_t{-1}) == "ffffffffffffffff");
  CHECK("%x"_pfmt(static_cast<signed char>(-1)) == "ff");
  CHECK("%o"_pfmt(static_cast<short>(-1)) == "177777");
}

TEST_CASE("%d accepts every integral type except bool and character types") {
  CHECK("%d %d %d %d"_pfmt(short{-1}, 2L, 3ULL, std::uint8_t{4}) == "-1 2 3 4");
  CHECK("%u %x"_pfmt(true, false) == "1 0");
  CHECK("%x"_pfmt('A') == "41");
}

TEST_CASE("floating point matches the C library") {
  for (double v : {0.0, -0.0, 1.0, -1.5, 3.14159265358979, 1e-7, 123456789.125, 1e100, 2.5, 0.5, 1.0 / 3}) {
    CAPTURE(v);
    CHECK_PRINTF("%f|%.0f|%.3f|%10.3f|%-10.3f|%010.3f|%+.2f|% .2f|%#.0f", v, v, v, v, v, v, v, v, v);
    CHECK_PRINTF("%e|%.0e|%.3e|%12.3e|%E|%#.0e|%+e", v, v, v, v, v, v, v);
    CHECK_PRINTF("%g|%.0g|%.3g|%10g|%G|%#g|%#.3g|%#.0g|%.10g", v, v, v, v, v, v, v, v, v);
    CHECK_PRINTF("%F|%+12.2F", v, v);
  }
  CHECK_PRINTF("%f|%e|%g", 1e-300, 1e-300, 1e-300);
  CHECK_PRINTF("%f", 1e300);
  CHECK_PRINTF("%.20f|%.17g", 0.1, 0.1);
  CHECK_PRINTF("%f|%.2f", 2.5f, 0.125f);
#if !defined(_LIBCPP_VERSION)
  CHECK_PRINTF("%Lf|%Le", 2.5L, 2.5L);
#endif
}

// Hexadecimal floats are spelled out: MSVC's C library pads %a to full precision.
TEST_CASE("hexadecimal floating point") {
  CHECK("%a|%a|%a|%a"_pfmt(1.0, -1.5, 0.5, 0.0) == "0x1p+0|-0x1.8p+0|0x1p-1|0x0p+0");
  CHECK("%A|%.3a|%#a"_pfmt(2.5, 1.0 / 3, 1.0) == "0X1.4P+1|0x1.555p-2|0x1.p+0");
  CHECK("[%12a]|[%012a]|[%-12a]"_pfmt(1.0, 1.0, 1.0) == "[      0x1p+0]|[0x0000001p+0]|[0x1p+0      ]");
}

TEST_CASE("infinity and NaN ignore zero padding") {
  double const inf = std::numeric_limits<double>::infinity();
  double const nan = std::numeric_limits<double>::quiet_NaN();
  CHECK_PRINTF("%f|%F|%e|%E|%g|%G|%08f|%-8f|%+f", inf, inf, inf, inf, inf, inf, inf, inf, inf);
  CHECK_PRINTF("%f|%F|%08f|%-8f", nan, nan, nan, nan);
  CHECK_PRINTF("%f|%08f", -inf, -inf);
}

TEST_CASE("a large precision falls back to a heap buffer") {
  CHECK_PRINTF("%.200f", 1.0 / 3);
  CHECK_PRINTF("%.330f|%.25f", 4.9e-324, 1.5e-12);  // tiny values stay on the fast path
  CHECK("%.400f"_pfmt(1.0).size() == 402);
}

TEST_CASE("characters, strings and pointers") {
  CHECK_PRINTF("%c|%3c|%-3c|", 'x', 'y', 'z');
  CHECK("%c%c%c"_pfmt(static_cast<signed char>('a'), static_cast<unsigned char>('b'), u8'c') == "abc");
  CHECK("%c|%c|%c"_pfmt(U'\u00E9', u'\u4E2D', U'\U0001F600') == "\u00E9|\u4E2D|\U0001F600");
  CHECK("%c"_pfmt(L'w') == "w");
  CHECK("[%3c]"_pfmt(U'\u4E2D') == "[ \u4E2D]");  // a wide character takes two columns
  // Values that are not Unicode scalar values become U+FFFD.
  CHECK("%c|%c"_pfmt(char32_t{0xD800}, char32_t{0x110000}) == "\uFFFD|\uFFFD");
  CHECK_PRINTF("%s|%8s|%-8s|%.2s|%8.2s|%-8.2s|%.0s|", "hello", "hello", "hello", "hello", "hello", "hello", "hello");

  char const* null = nullptr;
  CHECK("%s"_pfmt(null) == "(null)");
  char array[8] = "abc";
  CHECK("%s"_pfmt(array) == "abc");
  CHECK("%s %s %s"_pfmt("lit"s, "view"sv, static_cast<char*>(array)) == "lit view abc");

  int x = 0;
  CHECK("%p"_pfmt(&x) == std::format("{}", static_cast<void const*>(&x)));
  CHECK("%p"_pfmt(nullptr) == "0x0");
  CHECK("%-6p|"_pfmt(static_cast<void*>(nullptr)) == "0x0   |");
}

TEST_CASE("alignment extensions") {
  CHECK("[%^7d]"_pfmt(42) == "[  42   ]");
  CHECK("[%^7s]"_pfmt("abc") == "[  abc  ]");
  CHECK("[%>7s]"_pfmt("abc") == "[    abc]");
  CHECK("[%=+7d]"_pfmt(42) == "[+    42]");
  CHECK("[%=#8x]"_pfmt(255u) == "[0x    ff]");
  CHECK("[%=8.2f]"_pfmt(-1.5) == "[-   1.50]");
  CHECK("[%^08d]"_pfmt(42) == "[   42   ]");
}

TEST_CASE("positional arguments may repeat") {
  CHECK("%1$s-%1$s-%2$d"_pfmt("ab", 3) == "ab-ab-3");
  CHECK("%2% %1% %2%"_pfmt(1, "x") == "x 1 x");
  CHECK("%1% %1%"_pfmt("a") == "a a");
}

TEST_CASE("%N% formats like {}") {
  CHECK("%1%|%2%|%3%|%4%|%5%"_pfmt(42, 2.5, true, 'c', "s") == "42|2.5|true|c|s");
}
