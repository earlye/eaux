#include <eaux/text/static_format/static_format.hpp>

#include <doctest/doctest.h>

#include <charconv>
#include <format>
#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace eaux::text::static_format::literals;

namespace demo {

// Formattable only through std::formatter.
struct point {
  int x, y;
};

// Formattable only through operator<<.
struct tag {
  char const* name;
  friend std::ostream& operator<<(std::ostream& os, tag const& t) { return os << '<' << t.name << '>'; }
};

// A force in newtons, formattable through static_format's own formatter with a spec like
// ".3;kN": decimals, then the unit.
struct force {
  double newtons;
};

// Has both formatters, to show which one each route picks.
struct both {};

// Writes a number through operator<<, where a stream's locale could group its digits.
struct count {
  friend std::ostream& operator<<(std::ostream& os, count) { return os << 1234567; }
};

// Groups thousands with commas.
struct grouping : std::numpunct<char> {
  char do_thousands_sep() const override { return ','; }
  std::string do_grouping() const override { return "\3"; }
};

}  // namespace demo

template <>
struct std::formatter<demo::point> : std::formatter<std::string_view> {
  auto format(demo::point const& p, std::format_context& ctx) const {
    return std::formatter<std::string_view>::format(std::format("({}, {})", p.x, p.y), ctx);
  }
};

template <>
struct std::formatter<demo::both> : std::formatter<std::string_view> {
  auto format(demo::both const&, std::format_context& ctx) const {
    return std::formatter<std::string_view>::format("std", ctx);
  }
};

template <>
struct eaux::text::static_format::formatter<demo::force> {
  struct spec {
    int decimals = 1;
    bool kilo = false;
  };

  static constexpr spec parse(std::string_view text) {
    spec s;
    if (text.empty()) return s;
    if (text.size() < 2 || text[0] != '.' || text[1] < '0' || text[1] > '9') throw std::invalid_argument("bad force spec");
    s.decimals = text[1] - '0';
    auto const unit = text.substr(2);
    if (unit == ";kN") {
      s.kilo = true;
    } else if (!unit.empty() && unit != ";N") {
      throw std::invalid_argument("bad force unit");
    }
    return s;
  }

  template <class Out>
  static Out format(spec const& s, Out out, demo::force const& f) {
    char buffer[64];
    auto const value = s.kilo ? f.newtons / 1000 : f.newtons;
    auto const end = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, s.decimals).ptr;
    out = std::copy(buffer, end, out);
    std::string_view const unit = s.kilo ? " kN" : " N";
    return std::copy(unit.begin(), unit.end(), out);
  }
};

template <>
struct eaux::text::static_format::formatter<demo::both> {
  struct spec {};
  static constexpr spec parse(std::string_view) { return {}; }
  template <class Out>
  static Out format(spec, Out out, demo::both const&) {
    for (char c : std::string_view("static")) *out++ = c;
    return out;
  }
};

TEST_CASE("%v uses std::formatter, else operator<<") {
  CHECK("%v"_pfmt(demo::point{1, 2}) == "(1, 2)");
  CHECK("%v"_pfmt(demo::tag{"t"}) == "<t>");
  CHECK("[%8v]"_pfmt(demo::point{1, 2}) == "[  (1, 2)]");
  CHECK("[%-8v]"_pfmt(demo::tag{"t"}) == "[<t>     ]");
  CHECK("[%^9v]"_pfmt(demo::tag{"t"}) == "[   <t>   ]");
  CHECK("[%.4v]"_pfmt(demo::point{10, 20}) == "[(10,]");
  CHECK("%v"_pfmt(demo::both{}) == "std");
  CHECK("%v %v"_pfmt(42, 2.5) == "42 2.5");
}

TEST_CASE("%V parses its braced spec at compile time") {
  demo::force const f{12345.0};
  CHECK("%V"_pfmt(f) == "12345.0 N");
  CHECK("%{.3;kN}V"_pfmt(f) == "12.345 kN");
  CHECK("[%-12{.3;kN}V]"_pfmt(f) == "[12.345 kN   ]");
  CHECK("[%12{.0}V]"_pfmt(f) == "[     12345 N]");
  CHECK("[%.6{.3;kN}V]"_pfmt(f) == "[12.345]");
  CHECK("%V"_pfmt(demo::both{}) == "static");
}

TEST_CASE("brace fields on user types try static_format, then std, then operator<<") {
  CHECK("{}"_bfmt(demo::both{}) == "static");
  CHECK("{:.2;kN}"_bfmt(demo::force{1500}) == "1.50 kN");
  CHECK("{}|{:>10}"_bfmt(demo::point{1, 2}, demo::point{3, 4}) == "(1, 2)|    (3, 4)");
  CHECK("{}"_bfmt(demo::tag{"t"}) == "<t>");
  CHECK("%1%"_pfmt(demo::tag{"t"}) == "<t>");
}

TEST_CASE("operator<< output stays \"C\"-locale under a grouping global locale") {
  auto const previous = std::locale::global(std::locale(std::locale::classic(), new demo::grouping));
  std::ostringstream probe;
  probe << 1234567;
  CHECK(probe.str() == "1,234,567");  // the global locale really groups
  CHECK("%v"_pfmt(demo::count{}) == "1234567");
  CHECK("{}"_bfmt(demo::count{}) == "1234567");
  CHECK("%1%"_pfmt(demo::count{}) == "1234567");
  std::locale::global(previous);
}
