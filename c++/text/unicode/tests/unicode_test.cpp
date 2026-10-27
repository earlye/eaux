#include <eaux/text/unicode/unicode.hpp>

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

namespace u = eaux::text::unicode;
using namespace std::string_view_literals;

static_assert(u::version == "18.0.0");
static_assert(u::grapheme_break_property(U'\r') == u::grapheme_break::cr);
static_assert(u::grapheme_break_property(U'́') == u::grapheme_break::extend);
static_assert(u::grapheme_break_property(U'‍') == u::grapheme_break::zwj);
static_assert(u::grapheme_break_property(U'\U0001F1FA') == u::grapheme_break::regional_indicator);
static_assert(u::grapheme_break_property(U'가') == u::grapheme_break::lv);
static_assert(u::is_extended_pictographic(U'\U0001F600'));
static_assert(!u::is_extended_pictographic(U'a'));
static_assert(u::indic_conjunct_break_property(U'्') == u::indic_conjunct_break::linker);
static_assert(u::indic_conjunct_break_property(U'क') == u::indic_conjunct_break::consonant);

// Everything works in constant expressions, which static_format relies on for literals.
static_assert(u::display_width("abc") == 3);
static_assert(u::display_width("中文") == 4);
static_assert(u::next_grapheme_break("éx", 0) == 3);
static_assert(u::byte_prefix("中文", 4) == 3);

TEST_CASE("decode") {
  CHECK(u::decode("A", 0).code_point == U'A');
  auto const e = u::decode("é", 0);
  CHECK(e.code_point == U'é');
  CHECK(e.length == 2);
  CHECK(e.valid);
  auto const emoji = u::decode("\U0001F600", 0);
  CHECK(emoji.code_point == U'\U0001F600');
  CHECK(emoji.length == 4);

  for (auto bad : {"\x80"sv, "\xC0\x80"sv, "\xE0\x80\x80"sv, "\xED\xA0\x80"sv, "\xF4\x90\x80\x80"sv, "\xE2\x82"sv}) {
    auto const d = u::decode(bad, 0);
    CHECK_FALSE(d.valid);
    CHECK(d.length == 1);
    CHECK(d.code_point == U'�');
  }
}

TEST_CASE("ascii_prefix scans every alignment and length") {
  std::string s(100, 'a');
  for (std::size_t len = 0; len <= s.size(); ++len) {
    CHECK(u::ascii_prefix(std::string_view(s).substr(0, len)) == len);
  }
  for (std::size_t at = 0; at < s.size(); ++at) {
    std::string t = s;
    t[at] = '\xC3';
    CHECK(u::ascii_prefix(t) == at);
    CHECK_FALSE(u::is_ascii(t));
  }
}

TEST_CASE("display width follows [format.string.std]") {
  CHECK(u::display_width("") == 0);
  CHECK(u::display_width("hello") == 5);
  CHECK(u::display_width("café") == 4);
  CHECK(u::display_width("café") == 4);    // combining acute joins the e
  CHECK(u::display_width("σ_max") == 5);    // σ_max
  CHECK(u::display_width("µs") == 2);       // µs
  CHECK(u::display_width("中文") == 4);  // 中文
  CHECK(u::display_width("가") == 2);        // Hangul syllable
  CHECK(u::display_width("가") == 2);  // conjoining jamo: one cluster, wide lead
  CHECK(u::display_width("\U0001F600") == 2);    // 😀
  CHECK(u::display_width("\U0001F468‍\U0001F469‍\U0001F467") == 2);  // family: one cluster
  CHECK(u::display_width("䷀") == 2);        // Yijing hexagram, wide by the standard's own range
  CHECK(u::display_width("\U0001F1FA\U0001F1F8") == 1);  // flag: regional indicators are not wide
  CHECK(u::display_width("\xFF\xFE") == 2);      // malformed bytes count one each
}

TEST_CASE("width_prefix never splits a cluster") {
  auto const p = u::width_prefix("ééé", 2);
  CHECK(p.size == 6);
  CHECK(p.width == 2);

  auto const wide = u::width_prefix("中文x", 3);
  CHECK(wide.size == 3);
  CHECK(wide.width == 2);

  auto const ascii = u::width_prefix("abcdef", 4);
  CHECK(ascii.size == 4);
  CHECK(ascii.width == 4);

  auto const tail = u::width_prefix("abé", 3);
  CHECK(tail.size == 5);
  CHECK(tail.width == 3);

  auto const cut = u::width_prefix("abé", 2);
  CHECK(cut.size == 2);
}

TEST_CASE("byte_prefix stops at a cluster boundary") {
  CHECK(u::byte_prefix("abcdef", 3) == 3);
  CHECK(u::byte_prefix("abc", 10) == 3);
  CHECK(u::byte_prefix("ab\r\ncd", 3) == 2);
  CHECK(u::byte_prefix("abéx", 3) == 2);
  CHECK(u::byte_prefix("abéx", 4) == 2);
  CHECK(u::byte_prefix("abéx", 5) == 5);
  CHECK(u::byte_prefix("\U0001F600", 3) == 0);
}

TEST_CASE("graphemes iterates clusters") {
  std::vector<std::string_view> parts;
  for (auto g : u::graphemes("aé\U0001F1FA\U0001F1F8\r\n")) parts.push_back(g);
  CHECK(parts == std::vector<std::string_view>{"a", "é", "\U0001F1FA\U0001F1F8", "\r\n"});
}
