#include <eaux/text/ascii_string_view.hpp>

#include <doctest/doctest.h>

#include <format>
// doctest prints string_view values with operator<<, which MSVC only defines once <ostream> is in.
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>

using eaux::text::ascii_string_view;
using namespace eaux::text::literals;
using namespace std::string_view_literals;

static_assert("hello"_ascii.size() == 5);
static_assert("hello"_ascii == ascii_string_view::from("hello").value());
static_assert(ascii_string_view{}.empty());
static_assert(""_ascii.empty());
static_assert(ascii_string_view::from("caf\xc3\xa9").error() == 3);
static_assert(ascii_string_view::from("\x80").error() == 0);
static_assert("abc"_ascii < "abd"_ascii);
static_assert("hello"_ascii.substr(1, 3) == "ell"_ascii);
static_assert("abc"_ascii == "abc");
static_assert("abc" == "abc"_ascii);
static_assert("abc"_ascii != "abd"sv);
static_assert("abc"_ascii < "abd"sv);
static_assert("abd" > "abc"_ascii);

TEST_CASE("from accepts ASCII and reports the first non-ASCII byte") {
  std::string const ok = "plain text, punctuation & digits 0123\t\n\x7f";
  auto const valid = ascii_string_view::from(ok);
  REQUIRE(valid.has_value());
  CHECK(valid->view() == ok);
  CHECK(valid->data() == ok.data());

  std::string const bad = "na\xc3\xafve";
  auto const invalid = ascii_string_view::from(bad);
  REQUIRE_FALSE(invalid.has_value());
  CHECK(invalid.error() == 2);
}

TEST_CASE("embedded NUL is ASCII") {
  auto const s = "a\0b"_ascii;
  CHECK(s.size() == 3);
  CHECK(s[1] == '\0');
}

TEST_CASE("converts to string_view and iterates") {
  std::string_view const v = "abc"_ascii;
  CHECK(v == "abc"sv);
  std::string collected;
  for (char c : "xyz"_ascii) collected += c;
  CHECK(collected == "xyz");
}

TEST_CASE("std::format uses the string_view formatter") {
  CHECK(std::format("[{}]", "id"_ascii) == "[id]");
  CHECK(std::format("[{:>6}]", "id"_ascii) == "[    id]");
  CHECK(std::format("[{:*^7.3}]", "column"_ascii) == "[**col**]");
}

TEST_CASE("hashes like its string_view, so it can key unordered containers") {
  CHECK(std::hash<ascii_string_view>{}("key"_ascii) == std::hash<std::string_view>{}("key"));
  std::unordered_map<ascii_string_view, int> counts;
  ++counts["a"_ascii];
  ++counts["a"_ascii];
  ++counts["b"_ascii];
  CHECK(counts.at("a"_ascii) == 2);
  CHECK(counts.size() == 2);
}
