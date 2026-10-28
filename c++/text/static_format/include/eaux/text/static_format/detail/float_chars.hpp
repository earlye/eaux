#pragma once

#include <eaux/text/static_format/detail/segment.hpp>
#include <eaux/text/static_format/detail/third_party/dragonbox.h>
#include <eaux/text/static_format/detail/third_party/fmt_fixed_precision.hpp>

#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <type_traits>

// Float conversion: Dragonbox for shortest form and fmt's fixed-precision digit generation for
// %f, %e and %g (see docs/adr/0003), laid out to match std::to_chars byte for byte. x87 long
// double, hex floats and the few precisions the fast path cannot decide go to std::to_chars.
namespace eaux::text::static_format::detail {

template <class F>
using to_chars_float = std::conditional_t<std::is_same_v<F, long double> && sizeof(long double) == sizeof(double), double, F>;

// Returns the length written, or 0 when the buffer is too small (a conversion is never empty).
template <class F>
std::size_t to_chars_precision(char* first, std::size_t capacity, F value, std::chars_format format, std::size_t precision) {
  auto const v = static_cast<to_chars_float<F>>(value);
  auto const r = precision == no_precision ? std::to_chars(first, first + capacity, v, format)
                                           : std::to_chars(first, first + capacity, v, format, static_cast<int>(precision));
  return r.ec == std::errc{} ? static_cast<std::size_t>(r.ptr - first) : 0;
}

template <class F>
std::size_t to_chars_shortest(char* first, std::size_t capacity, F value) {
  auto const r = std::to_chars(first, first + capacity, static_cast<to_chars_float<F>>(value));
  return r.ec == std::errc{} ? static_cast<std::size_t>(r.ptr - first) : 0;
}

namespace float_layout {

// float, double, and long double where it is double; x87 long double stays on std::to_chars.
template <class F>
inline constexpr bool fast = std::is_same_v<F, float> || std::is_same_v<to_chars_float<F>, double>;

inline constexpr char digit_pairs[] =
    "0001020304050607080910111213141516171819"
    "2021222324252627282930313233343536373839"
    "4041424344454647484950515253545556575859"
    "6061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

inline int count_digits(std::uint64_t n) noexcept {
  static constexpr std::uint64_t powers[] = {
      1ULL,          10ULL,          100ULL,          1000ULL,          10000ULL,
      100000ULL,     1000000ULL,     10000000ULL,     100000000ULL,     1000000000ULL,
      10000000000ULL, 100000000000ULL, 1000000000000ULL, 10000000000000ULL, 100000000000000ULL,
      1000000000000000ULL, 10000000000000000ULL, 100000000000000000ULL, 1000000000000000000ULL,
      10000000000000000000ULL};
  int const t = (64 - std::countl_zero(n | 1)) * 1233 >> 12;
  return t + (n >= powers[t] ? 1 : 0);
}

// Writes the `count` decimal digits of n to out.
inline void write_digits(char* out, std::uint64_t n, int count) noexcept {
  char* p = out + count;
  while (n >= 100) {
    p -= 2;
    std::memcpy(p, digit_pairs + (n % 100) * 2, 2);
    n /= 100;
  }
  if (n >= 10) {
    p -= 2;
    std::memcpy(p, digit_pairs + n * 2, 2);
  } else {
    *--p = static_cast<char>('0' + n);
  }
}

// Writes e+XX or e-XX (three digits when needed), as printf does.
inline char* write_exponent(char* out, int x) noexcept {
  *out++ = 'e';
  if (x < 0) {
    *out++ = '-';
    x = -x;
  } else {
    *out++ = '+';
  }
  if (x >= 100) {
    *out++ = static_cast<char>('0' + x / 100);
    x %= 100;
  }
  std::memcpy(out, digit_pairs + x * 2, 2);
  return out + 2;
}

// Lays out m * 10^e (m > 0) as std::to_chars' shortest form does: fixed or scientific, whichever
// is shorter, fixed on a tie. Returns 0 when fixed wins with digits before trailing zeros and
// `big` says the value is an integer too large for every integer to be representable: there
// std::to_chars prints the exact integer, not the shortest digits padded with zeros.
inline std::size_t shortest_layout(char* out, std::uint64_t m, int e, bool big) noexcept {
  int const n = count_digits(m);
  int const x = e + n - 1;
  int const sci = n + (n > 1 ? 1 : 0) + 2 + (x >= 100 || x <= -100 ? 3 : 2);
  int const fix = e >= 0 ? n + e : x >= 0 ? n + 1 : n + 1 - x;
  if (fix <= sci) {
    if (e >= 0) {
      if (big && e > 0) return 0;
      write_digits(out, m, n);
      std::memset(out + n, '0', static_cast<std::size_t>(e));
      return static_cast<std::size_t>(fix);
    }
    if (x >= 0) {
      write_digits(out + 1, m, n);
      std::memmove(out, out + 1, static_cast<std::size_t>(x + 1));
      out[x + 1] = '.';
    } else {
      out[0] = '0';
      out[1] = '.';
      std::memset(out + 2, '0', static_cast<std::size_t>(-x - 1));
      write_digits(out + 1 - x, m, n);
    }
    return static_cast<std::size_t>(fix);
  }
  write_digits(out + 1, m, n);
  out[0] = out[1];
  std::size_t size = 1;
  if (n > 1) {
    out[1] = '.';
    size = static_cast<std::size_t>(n) + 1;
  }
  return static_cast<std::size_t>(write_exponent(out + size, x) - out);
}

// floor(log10(v)) or one less, for a positive finite v: floor(log2(v) * log10(2)).
inline int log10_estimate(double v) noexcept {
  auto const bits = std::bit_cast<std::uint64_t>(v);
  int const field = static_cast<int>(bits >> 52);
  int const log2 = field != 0 ? field - 1023 : -1011 - std::countl_zero(bits & ((std::uint64_t(1) << 52) - 1));
  return (log2 * 315653) >> 20;
}

// Lays out `count` digits, the integer round(v * 10^p), as %.pf. The digits are at out[0..count).
inline std::size_t fixed_layout(char* out, int count, int p) noexcept {
  if (p == 0) {
    if (count != 0) return static_cast<std::size_t>(count);
    out[0] = '0';
    return 1;
  }
  if (count > p) {
    int const integral = count - p;
    std::memmove(out + integral + 1, out + integral, static_cast<std::size_t>(p));
    out[integral] = '.';
    return static_cast<std::size_t>(count) + 1;
  }
  std::memmove(out + 2 + (p - count), out, static_cast<std::size_t>(count));
  out[0] = '0';
  out[1] = '.';
  std::memset(out + 2, '0', static_cast<std::size_t>(p - count));
  return static_cast<std::size_t>(p) + 2;
}

// Lays out n significant digits (at out[0..n)) with decimal exponent x as %e.
inline std::size_t scientific_layout(char* out, int n, int x) noexcept {
  std::size_t size = 1;
  if (n > 1) {
    std::memmove(out + 2, out + 1, static_cast<std::size_t>(n) - 1);
    out[1] = '.';
    size = static_cast<std::size_t>(n) + 1;
  }
  return static_cast<std::size_t>(write_exponent(out + size, x) - out);
}

inline std::size_t zero_chars(char* out, std::chars_format format, int p) noexcept {
  out[0] = '0';
  if (format == std::chars_format::general || p == 0) {
    if (format != std::chars_format::scientific) return 1;
    std::memcpy(out + 1, "e+00", 4);
    return 5;
  }
  out[1] = '.';
  std::memset(out + 2, '0', static_cast<std::size_t>(p));
  if (format == std::chars_format::fixed) return static_cast<std::size_t>(p) + 2;
  std::memcpy(out + 2 + p, "e+00", 4);
  return static_cast<std::size_t>(p) + 6;
}

}  // namespace float_layout

namespace float_digits {

inline constexpr int fallback = -100000;

// n significant digits of v > 0 at out[0..n); returns their decimal exponent, or `fallback`.
inline int scientific(double v, int n, char* out) noexcept {
  int count = n;
  int exp;
  if (!fmt_port::format_float(v, count, false, out, exp)) return fallback;
  return exp + n - 1;
}

// The digits of round(v * 10^p) for v > 0, without leading zeros; returns their count, or -1.
inline int fixed(double v, int p, char* out) noexcept {
  int count = p;
  int exp;
  if (!fmt_port::format_float(v, count, true, out, exp)) return -1;
  return count == 1 && out[0] == '0' ? 0 : count;
}

}  // namespace float_digits

// v >= 0 and finite. Returns 0 when `capacity` is too small.
inline std::size_t precision_chars(char* out, std::size_t capacity, double v, std::chars_format format, int p) {
  namespace layout = float_layout;
  int const integral = v < 1 ? 1 : layout::log10_estimate(v) + 2;
  if (capacity < static_cast<std::size_t>((format == std::chars_format::fixed ? integral : 1) + p + 8)) return 0;
  if (v == 0) return layout::zero_chars(out, format, p);
  if (format == std::chars_format::fixed) {
    int const count = float_digits::fixed(v, p, out);
    if (count < 0) return to_chars_precision(out, capacity, v, format, static_cast<std::size_t>(p));
    return layout::fixed_layout(out, count, p);
  }
  if (format == std::chars_format::scientific) {
    int const x = float_digits::scientific(v, p + 1, out);
    if (x == float_digits::fallback) return to_chars_precision(out, capacity, v, format, static_cast<std::size_t>(p));
    return layout::scientific_layout(out, p + 1, x);
  }
  // %g: P significant digits, then %f if P > X >= -4, else %e, without trailing zeros.
  int const n = p == 0 ? 1 : p;
  int const x = float_digits::scientific(v, n, out);
  if (x == float_digits::fallback) return to_chars_precision(out, capacity, v, format, static_cast<std::size_t>(p));
  if (n > x && x >= -4) {
    int const decimals = n - 1 - x;
    std::size_t size = layout::fixed_layout(out, n, decimals);
    if (decimals > 0) {
      while (out[size - 1] == '0') --size;
      if (out[size - 1] == '.') --size;
    }
    return size;
  }
  int k = n;
  while (k > 1 && out[k - 1] == '0') --k;
  return layout::scientific_layout(out, k, x);
}

template <class F>
std::size_t shortest_decimal_chars(char* out, F value) {
  namespace db = jkj::dragonbox;
  auto const d = db::to_decimal(value, db::policy::sign::ignore, db::policy::trailing_zero::remove);
  constexpr F big = std::is_same_v<F, float> ? 0x1p24f : 0x1p53;
  return float_layout::shortest_layout(out, d.significand, d.exponent, value >= big);
}

// Returns the length written, or 0 when the buffer is too small (a conversion is never empty).
template <class F>
std::size_t float_chars(char* first, std::size_t capacity, F value, std::chars_format format, std::size_t precision) {
  if constexpr (float_layout::fast<F>) {
    if (format != std::chars_format::hex && precision <= 100000) {
      return precision_chars(first, capacity, static_cast<double>(value), format, static_cast<int>(precision));
    }
  }
  return to_chars_precision(first, capacity, value, format, precision);
}

template <class F>
std::size_t shortest_chars(char* first, std::size_t capacity, F value) {
  if constexpr (float_layout::fast<F>) {
    using T = std::conditional_t<std::is_same_v<F, float>, float, double>;
    if (capacity >= 32) {
      if (value == 0) {
        first[0] = '0';
        return 1;
      }
      if (std::size_t const size = shortest_decimal_chars(first, static_cast<T>(value)); size != 0) return size;
    }
  }
  return to_chars_shortest(first, capacity, value);
}

}  // namespace eaux::text::static_format::detail
