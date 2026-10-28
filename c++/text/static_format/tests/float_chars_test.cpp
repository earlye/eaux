#include <eaux/text/static_format/static_format.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// The vendored float conversion (Dragonbox and the port of fmt's fixed-precision digits) must
// write what std::to_chars writes, digit for digit.

using namespace eaux::text::static_format::literals;
namespace detail = eaux::text::static_format::detail;

namespace {

constexpr std::size_t capacity = 1100;  // %.17f of DBL_MAX, with room to spare

// Values that catch layout and rounding mistakes: zeros, the extremes, powers of ten and their
// neighbours, exact binary ties (x.5, x.125), decimal near-ties (x.xxx5 rounded to binary), and
// runs of nines that carry into a new digit.
std::vector<double> edge_values() {
  std::vector<double> v = {0.0,
                           -0.0,
                           std::numeric_limits<double>::denorm_min(),
                           std::numeric_limits<double>::min(),
                           std::nextafter(std::numeric_limits<double>::min(), 0.0),
                           std::numeric_limits<double>::max(),
                           std::numeric_limits<double>::epsilon(),
                           0x1p53,
                           0x1p53 + 2,
                           0x1p63,
                           0x1p64,
                           0x1p24,
                           1.0 / 3,
                           2.0 / 3,
                           0.1,
                           0.2,
                           0.3};
  for (int e = -324; e <= 308; ++e) {
    double const p = std::strtod(("1e" + std::to_string(e)).c_str(), nullptr);
    v.insert(v.end(), {p, std::nextafter(p, 0.0), std::nextafter(p, 2 * p), 5 * p, 9.5 * p});
  }
  for (int k = 0; k < 64; ++k) {
    v.push_back(k + 0.5);
    v.push_back(k + 0.125);
    v.push_back(k + 0.375);
    v.push_back(k / 1024.0);
  }
  std::mt19937_64 rng(20261027);
  for (int decimals = 0; decimals <= 17; ++decimals) {
    for (int i = 0; i < 40; ++i) {
      // d.ddd...5 with `decimals` digits after the point, then a 5.
      std::string s = std::to_string(rng() % 1000) + ".";
      for (int d = 0; d < decimals; ++d) s += static_cast<char>('0' + rng() % 10);
      v.push_back(std::strtod((s + "5").c_str(), nullptr));
      v.push_back(std::strtod((s + "49999999999").c_str(), nullptr));
      v.push_back(std::strtod((std::string(1 + rng() % 4, '9') + "." + std::string(static_cast<std::size_t>(decimals), '9') + "5").c_str(), nullptr));
    }
  }
  std::erase_if(v, [](double d) { return !std::isfinite(d); });
  return v;
}

// Random bit patterns (every exponent equally likely) and values like the benchmark's: 17
// significant digits between 1e-9 and 1e9.
std::vector<double> random_values(int count) {
  std::mt19937_64 rng(0x5eed);
  std::uniform_real_distribution<double> mantissa(1.0, 10.0);
  std::uniform_int_distribution<int> exponent(-9, 9);
  std::vector<double> v;
  while (static_cast<int>(v.size()) < count) {
    double const bits = std::bit_cast<double>(rng());
    if (std::isfinite(bits)) v.push_back(std::fabs(bits));
    v.push_back(mantissa(rng) * std::pow(10.0, exponent(rng)));
  }
  return v;
}

template <class F>
std::string ours(F value, std::chars_format format, std::size_t precision) {
  std::string out(capacity, '\0');
  out.resize(precision == detail::no_precision ? detail::shortest_chars(out.data(), capacity, value)
                                               : detail::float_chars(out.data(), capacity, value, format, precision));
  return out;
}

template <class F>
std::string reference(F value, std::chars_format format, std::size_t precision) {
  std::string out(capacity, '\0');
  auto const r = precision == detail::no_precision
                     ? std::to_chars(out.data(), out.data() + capacity, value)
                     : std::to_chars(out.data(), out.data() + capacity, value, format, static_cast<int>(precision));
  out.resize(static_cast<std::size_t>(r.ptr - out.data()));
  return out;
}

struct mismatches {
  int count = 0;
  std::string first;

  template <class F>
  void check(F value, std::chars_format format, std::size_t precision) {
    F const magnitude = std::fabs(value);
    std::string const got = ours(magnitude, format, precision);
    std::string const want = reference(magnitude, format, precision);
    if (got == want) return;
    if (count++ < 5) {
      char hex[64];
      std::snprintf(hex, sizeof hex, "%a", static_cast<double>(magnitude));
      first += std::string(hex) + " format " + std::to_string(static_cast<int>(format)) + " precision " +
               (precision == detail::no_precision ? std::string("shortest") : std::to_string(precision)) + ": got " + got +
               ", want " + want + "\n";
    }
  }
};

void check_precisions(std::vector<double> const& values) {
  mismatches m;
  for (double const v : values) {
    for (std::size_t p = 0; p <= 17; ++p) {
      m.check(v, std::chars_format::fixed, p);
      m.check(v, std::chars_format::scientific, p);
      m.check(v, std::chars_format::general, p);
    }
    m.check(v, std::chars_format::general, detail::no_precision);
    if (float const f = static_cast<float>(v); std::isfinite(f)) {
      m.check(f, std::chars_format::general, detail::no_precision);
      m.check(f, std::chars_format::fixed, 6);
    }
  }
  INFO(m.first);
  CHECK(m.count == 0);
}

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

TEST_CASE("float conversion matches std::to_chars on edge cases, precisions 0-17") { check_precisions(edge_values()); }

TEST_CASE("float conversion matches std::to_chars on random doubles, precisions 0-17") {
  check_precisions(random_values(40000));
}

TEST_CASE("float conversion matches std::to_chars in shortest form") {
  mismatches m;
  std::mt19937_64 rng(42);
  for (int i = 0; i < 400000; ++i) {
    double const d = std::bit_cast<double>(rng());
    if (std::isfinite(d)) m.check(d, std::chars_format::general, detail::no_precision);
    float const f = std::bit_cast<float>(static_cast<std::uint32_t>(rng()));
    if (std::isfinite(f)) m.check(f, std::chars_format::general, detail::no_precision);
  }
  for (double const d : edge_values()) {
    m.check(d, std::chars_format::general, detail::no_precision);
    if (float const f = static_cast<float>(d); std::isfinite(f)) m.check(f, std::chars_format::general, detail::no_precision);
  }
  // Integers from 2^53 (2^24 for float), which std::to_chars prints exactly in fixed form.
  for (int i = 0; i < 100000; ++i) {
    m.check(static_cast<double>(rng() >> (rng() % 40)), std::chars_format::general, detail::no_precision);
    m.check(static_cast<float>(rng() >> (rng() % 60)), std::chars_format::general, detail::no_precision);
  }
  INFO(m.first);
  CHECK(m.count == 0);
}

// %#g by C's rule, from std::to_chars: %e with P - 1 digits gives the exponent X; if P > X >= -4,
// %f with P - 1 - X digits instead; then a point if there is none. glibc's printf is no
// reference here: it drops a zero when rounding carries (%#.2g of 99.5 is "1.e+02").
std::string alternate_g(double v, int p) {
  p = p == 0 ? 1 : p;
  std::string s = reference(std::fabs(v), std::chars_format::scientific, static_cast<std::size_t>(p - 1));
  int const x = std::stoi(s.substr(s.find('e') + 1));
  if (p > x && x >= -4) s = reference(std::fabs(v), std::chars_format::fixed, static_cast<std::size_t>(p - 1 - x));
  if (s.find('.') == std::string::npos) s.insert(std::min(s.find('e'), s.size()), ".");
  return (std::signbit(v) ? "-" : "") + s;
}

#define CHECK_ALT_G(p)                                                                     \
  for (double const v : values) {                                                         \
    if (std::string const got = "%#." #p "g"_pfmt(v); got != alternate_g(v, p)) {          \
      if (wrong++ < 5) first += got + " for %#." #p "g of " + c_printf("%a", v) + "\n"; \
    }                                                                                     \
  }

TEST_CASE("%#g matches std::to_chars, precisions 0-17") {
  std::vector<double> values = edge_values();
  std::vector<double> const random = random_values(4000);
  values.insert(values.end(), random.begin(), random.end());
  int wrong = 0;
  std::string first;
  CHECK_ALT_G(0) CHECK_ALT_G(1) CHECK_ALT_G(2) CHECK_ALT_G(3) CHECK_ALT_G(4) CHECK_ALT_G(5);
  CHECK_ALT_G(6) CHECK_ALT_G(7) CHECK_ALT_G(8) CHECK_ALT_G(9) CHECK_ALT_G(10) CHECK_ALT_G(11);
  CHECK_ALT_G(12) CHECK_ALT_G(13) CHECK_ALT_G(14) CHECK_ALT_G(15) CHECK_ALT_G(16) CHECK_ALT_G(17);
  INFO(first);
  CHECK(wrong == 0);
}

TEST_CASE("long double and %a stay on std::to_chars") {
  long double const x = 1.0L / 3;
  CHECK("%Lf|%.3Le|%Lg"_pfmt(x, x, x) == c_printf("%Lf|%.3Le|%Lg", x, x, x));
  CHECK("%a|%.3A"_pfmt(0.1, 0.1) == c_printf("%a|%.3A", 0.1, 0.1));
}
