// Runs every case in the Unicode Consortium's GraphemeBreakTest.txt for the pinned version.
#include <eaux/text/unicode/unicode.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void append_utf8(std::string& out, char32_t cp) {
  auto const put = [&](unsigned v) { out.push_back(static_cast<char>(v)); };
  if (cp < 0x80) {
    put(cp);
  } else if (cp < 0x800) {
    put(0xC0 | (cp >> 6)), put(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    put(0xE0 | (cp >> 12)), put(0x80 | ((cp >> 6) & 0x3F)), put(0x80 | (cp & 0x3F));
  } else {
    put(0xF0 | (cp >> 18)), put(0x80 | ((cp >> 12) & 0x3F)), put(0x80 | ((cp >> 6) & 0x3F)), put(0x80 | (cp & 0x3F));
  }
}

struct test_case {
  std::string line;
  std::string text;
  std::vector<std::size_t> breaks;  // byte offsets of every boundary after the start
};

std::vector<test_case> load() {
  std::ifstream in(EAUX_GRAPHEME_BREAK_TEST);
  REQUIRE_MESSAGE(in, "cannot open " EAUX_GRAPHEME_BREAK_TEST);
  std::vector<test_case> cases;
  for (std::string line; std::getline(in, line);) {
    auto const data = line.substr(0, line.find('#'));
    if (data.find_first_not_of(" \t") == std::string::npos) continue;
    test_case c{line, {}, {}};
    std::istringstream tokens(data);
    for (std::string token; tokens >> token;) {
      if (token == "\xC3\xB7") {  // ÷
        if (!c.text.empty()) c.breaks.push_back(c.text.size());
      } else if (token != "\xC3\x97") {  // ×
        append_utf8(c.text, static_cast<char32_t>(std::stoul(token, nullptr, 16)));
      }
    }
    cases.push_back(std::move(c));
  }
  return cases;
}

}  // namespace

TEST_CASE("GraphemeBreakTest.txt") {
  auto const cases = load();
  CHECK(cases.size() > 800);
  std::size_t failures = 0;
  for (auto const& c : cases) {
    std::vector<std::size_t> breaks;
    for (std::size_t pos = 0; pos < c.text.size();) {
      pos = eaux::text::unicode::next_grapheme_break(c.text, pos);
      breaks.push_back(pos);
    }
    if (breaks != c.breaks) {
      ++failures;
      FAIL_CHECK(c.line);
    }
  }
  CHECK(failures == 0);
}
