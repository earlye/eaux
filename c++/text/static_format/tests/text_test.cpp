// Width, precision and fill count display width over grapheme clusters, in both syntaxes.
#include <eaux/text/static_format/static_format.hpp>

#include <doctest/doctest.h>

#include <format>
#include <string>
#include <string_view>

using namespace eaux::text::static_format::literals;
using namespace eaux::text::literals;
using namespace std::string_literals;

TEST_CASE("width counts display width, not bytes") {
  CHECK("[%-6s]"_pfmt("\u00B5s") == "[\u00B5s    ]");                // µs is two columns
  CHECK("[%8s]"_pfmt("\u03C3_max") == "[   \u03C3_max]");             // σ_max is five
  CHECK("[%4s]"_pfmt("\u4E2D") == "[  \u4E2D]");                       // 中 is two
  CHECK("[%-5s]"_pfmt("cafe\u0301") == "[cafe\u0301 ]");               // the accent joins the e
  CHECK("[%4s]"_pfmt("\U0001F468\u200D\U0001F469\u200D\U0001F467") == "[  \U0001F468\u200D\U0001F469\u200D\U0001F467]");
  CHECK("[{:<6}]"_bfmt("\u00B5s") == "[\u00B5s    ]");
  CHECK("[{:^6}]"_bfmt("\u4E2D") == "[  \u4E2D  ]");
}

TEST_CASE("precision is a width budget that never splits a cluster") {
  CHECK("[%.3s]"_pfmt("e\u0301e\u0301e\u0301e\u0301") == "[e\u0301e\u0301e\u0301]");
  CHECK("[%.3s]"_pfmt("\u4E2D\u6587") == "[\u4E2D]");
  CHECK("[%-4.3s]"_pfmt("\u4E2D\u6587") == "[\u4E2D  ]");
  CHECK("[{:.1}]"_bfmt("\U0001F600x") == "[]");
  CHECK("[{:.2}]"_bfmt("\U0001F600x") == "[\U0001F600]");
}

TEST_CASE("UTF-8 behaviour agrees with std::format") {
  // libstdc++ and MSVC implement the same [format.string.std] width rules (their Unicode version
  // may lag, so these use long-assigned characters).
  for (std::string const& s : {"\u00B5s"s, "\u4E2D\u6587"s, "cafe\u0301"s, "\U0001F600"s, "\uAC00\u1100\u1161"s, "a\u0308b"s}) {
    CAPTURE(s);
    CHECK("{:>8}|{:<8}|{:^9}|{:.3}|{:*^6.2}"_bfmt(s, s, s, s, s) == std::format("{:>8}|{:<8}|{:^9}|{:.3}|{:*^6.2}", s, s, s, s, s));
  }
}

TEST_CASE("ascii_string_view takes the ASCII path in both syntaxes") {
  auto const label = "column"_ascii;
  CHECK("[%-8s]"_pfmt(label) == "[column  ]");
  CHECK("[%8.3{ascii}s]"_pfmt(label) == "[     col]");
  CHECK("[%-8{ascii}s]"_pfmt(label) == "[column  ]");
  CHECK("[{:>8}]"_bfmt(label) == "[  column]");
  auto const checked = eaux::text::ascii_string_view::from(std::string_view("runtime"));
  REQUIRE(checked);
  CHECK("%{ascii}s"_pfmt(*checked) == "runtime");
}

TEST_CASE("UTF-8 in format string literals passes through") {
  CHECK("\u00B5=%.1f \u03C3"_pfmt(2.5) == "\u00B5=2.5 \u03C3");
  CHECK("{}\u2192{}"_bfmt(1, 2) == "1\u21922");
}

TEST_CASE("malformed UTF-8 passes through unchanged") {
  std::string const bad = "a\xff\xfe" "b";
  CHECK("%s"_pfmt(bad) == bad);
  CHECK("[%6s]"_pfmt(bad) == "[  " + bad + "]");  // one column per malformed byte
  CHECK("[%.2s]"_pfmt(bad) == "[a\xff]");
}
