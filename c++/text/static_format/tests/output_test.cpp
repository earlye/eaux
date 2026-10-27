#include <eaux/text/static_format/static_format.hpp>

#include <doctest/doctest.h>

#include <array>
#include <deque>
#include <iterator>
#include <list>
#include <string>
#include <string_view>
#include <vector>

using namespace eaux::text::static_format::literals;
using namespace eaux::text::literals;

TEST_CASE("to writes through pointers, contiguous iterators and other output iterators") {
  char buffer[32];
  char* const end = "%d-%02d"_pfmt.to(buffer, 2000, 1);
  CHECK(std::string_view(buffer, static_cast<std::size_t>(end - buffer)) == "2000-01");

  std::array<char, 32> array{};
  auto const it = "{}:{}"_bfmt.to(array.begin(), "k", 7);
  CHECK(std::string_view(array.data(), static_cast<std::size_t>(it - array.begin())) == "k:7");

  std::string s = "> ";
  "%s=%d"_pfmt.to(std::back_inserter(s), "x", 1);
  CHECK(s == "> x=1");

  std::list<char> list;
  "%5.1f"_pfmt.to(std::back_inserter(list), 2.25);
  CHECK(std::string(list.begin(), list.end()) == "  2.2");
}

TEST_CASE("to_n reports the untruncated size") {
  char buffer[64];
  auto const all = "%s=%d"_pfmt.to_n(buffer, 64, "abc", 12345);
  CHECK(all.size == 9);
  CHECK(std::string_view(buffer, static_cast<std::size_t>(all.out - buffer)) == "abc=12345");

  auto const cut = "%s=%d"_pfmt.to_n(buffer, 6, "abc", 12345);
  CHECK(cut.size == 9);
  CHECK(std::string_view(buffer, static_cast<std::size_t>(cut.out - buffer)) == "abc=12");

  auto const none = "%s"_pfmt.to_n(buffer, 0, "abc");
  CHECK(none.out == buffer);
  CHECK(none.size == 3);
  CHECK("%s"_pfmt.to_n(buffer, -5, "abc").out == buffer);
}

TEST_CASE("to_n never splits a grapheme cluster") {
  char buffer[64];
  auto const view = [&](char* end) { return std::string_view(buffer, static_cast<std::size_t>(end - buffer)); };

  // In an argument.
  CHECK(view("%s"_pfmt.to_n(buffer, 2, "a\u00E9").out) == "a");
  CHECK(view("%s"_pfmt.to_n(buffer, 3, "ae\u0301").out) == "a");
  CHECK(view("%s"_pfmt.to_n(buffer, 4, "ae\u0301").out) == "ae\u0301");
  CHECK(view("%s"_pfmt.to_n(buffer, 5, "\U0001F1FA\U0001F1F8").out) == "");

  // In a literal.
  CHECK(view("caf\u00E9 %d"_pfmt.to_n(buffer, 4, 1).out) == "caf");
  CHECK(view("caf\u00E9 %d"_pfmt.to_n(buffer, 5, 1).out) == "caf\u00E9");
  CHECK(view("e\u0301e\u0301{}"_bfmt.to_n(buffer, 5, 1).out) == "e\u0301");

  // In a user type's output.
  CHECK(view("%v"_pfmt.to_n(buffer, 2, std::string_view("a\u00E9")).out) == "a");

  // In a multi-byte fill.
  CHECK(view("{:\u00B7>4}"_bfmt.to_n(buffer, 5, 1).out) == "\u00B7\u00B7");

  // Across a literal and an argument, in either order.
  CHECK(view("e%s"_pfmt.to_n(buffer, 1, "\u0301").out) == "");
  CHECK(view("%s\u0301"_pfmt.to_n(buffer, 1, "e").out) == "");
  CHECK(view("{}\u0301x"_bfmt.to_n(buffer, 3, "e").out) == "e\u0301");

  // Nothing follows a cut, even a piece that would fit.
  CHECK(view("%s%s"_pfmt.to_n(buffer, 2, "\u00E9\u00E9", "x").out) == "\u00E9");
}

TEST_CASE("ASCII cuts anywhere except inside CR LF") {
  char buffer[16];
  auto const r = "%{ascii}s|%08.3f"_pfmt.to_n(buffer, 7, "abcdef"_ascii, 3.14159);
  CHECK(std::string_view(buffer, static_cast<std::size_t>(r.out - buffer)) == "abcdef|");
  CHECK(r.size == 15);

  auto const crlf = "ab\r\n%d"_pfmt.to_n(buffer, 3, 1);
  CHECK(std::string_view(buffer, static_cast<std::size_t>(crlf.out - buffer)) == "ab");
  CHECK("[%.3s]"_pfmt("ab\r\ncd") == "[ab]");
  CHECK("[%.3s]"_pfmt("ab\r\ncd"_ascii) == "[ab]");
  CHECK("[%.4s]"_pfmt("ab\r\ncd"_ascii) == "[ab\r\n]");
}

TEST_CASE("to_n through a non-contiguous iterator") {
  std::deque<char> out;
  auto const r = "%s"_pfmt.to_n(std::back_inserter(out), 3, "ab\u00E9");
  CHECK(r.size == 4);
  CHECK(std::string(out.begin(), out.end()) == "ab");
}

TEST_CASE("formats are values usable many times") {
  constexpr auto row = "%-4s|%3d"_pfmt;
  std::vector<std::string> lines;
  for (int i = 0; i < 3; ++i) lines.push_back(row("r", i));
  CHECK(lines == std::vector<std::string>{"r   |  0", "r   |  1", "r   |  2"});
}

TEST_CASE("std::string results longer than the stack buffer") {
  std::string const filled = "%600d|%s"_pfmt(7, "end");
  CHECK(filled == std::string(599, ' ') + "7|end");
  std::string const long_text(700, 'x');
  std::string const appended = "<%s>%-520s|"_pfmt(long_text, "y");
  CHECK(appended == "<" + long_text + ">y" + std::string(519, ' ') + "|");
}
