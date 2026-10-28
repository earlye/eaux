// Ported into eaux::text::static_format from {fmt} 12.2.0 (https://github.com/fmtlib/fmt,
// include/fmt/format.h: the fast path of detail::format_float, written by Junekey Jeon), used
// under fmt's MIT licence, reproduced below. Changes from upstream: only the path for double that
// avoids Dragon4 is kept; where fmt falls back to Dragon4 this returns false and the caller uses
// std::to_chars instead. The cache and the 128-bit helpers come from the vendored Dragonbox,
// which fmt's are copies of.
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once

#include <eaux/text/static_format/detail/third_party/dragonbox.h>

#include <bit>
#include <cstdint>
#include <cstring>

namespace eaux::text::static_format::detail::fmt_port {

inline void write2digits(char* out, std::uint32_t value) noexcept {
  static constexpr char digits[] =
      "0001020304050607080910111213141516171819"
      "2021222324252627282930313233343536373839"
      "4041424344454647484950515253545556575859"
      "6061626364656667686970717273747576777879"
      "8081828384858687888990919293949596979899";
  std::memcpy(out, digits + value * 2, 2);
}

using db_uint128 = jkj::dragonbox::detail::wuint::uint128;

// 10^k for k in (326, 341]: Dragonbox's table stops at 326, which shortest form needs, while the
// fixed-precision digits of subnormals need up to 341. Copied from fmt's full cache.
inline db_uint128 cached_power(int k) noexcept {
  using cache = jkj::dragonbox::cache_holder<jkj::dragonbox::ieee754_binary64>;
  static constexpr db_uint128 extra[] = {
    {0x9a65406d44a5c903, 0x737f74f1dc043329}, {0xc0fe908895cf3b44, 0x505f522e53053ff3},
    {0xf13e34aabb430a15, 0x647726b9e7c68ff0}, {0x96c6e0eab509e64d, 0x5eca783430dc19f6},
    {0xbc789925624c5fe0, 0xb67d16413d132073}, {0xeb96bf6ebadf77d8, 0xe41c5bd18c57e890},
    {0x933e37a534cbaae7, 0x8e91b962f7b6f15a}, {0xb80dc58e81fe95a1, 0x723627bbb5a4adb1},
    {0xe61136f2227e3b09, 0xcec3b1aaa30dd91d}, {0x8fcac257558ee4e6, 0x213a4f0aa5e8a7b2},
    {0xb3bd72ed2af29e1f, 0xa988e2cd4f62d19e}, {0xe0accfa875af45a7, 0x93eb1b80a33b8606},
    {0x8c6c01c9498d8b88, 0xbc72f130660533c4}, {0xaf87023b9bf0ee6a, 0xeb8fad7c7f8680b5},
    {0xdb68c2ca82ed2a05, 0xa67398db9f6820e2},
  };
  return k <= cache::max_k ? cache::cache[static_cast<std::size_t>(k - cache::min_k)]
                           : extra[static_cast<std::size_t>(k - cache::max_k - 1)];
}

// The kth entry is the smallest integer such that the upper 32 bits of 10^(k+1) times it is
// strictly bigger than 5 * 10^k.
constexpr std::uint32_t fractional_part_rounding_thresholds(int index) noexcept {
  return std::uint32_t(u"\x9999\x828f\x8041\x8006\x8000\x8000\x8000\x8000"[index]) << 16u |
         std::uint32_t(u"\x999a\x5c29\x8938\x8db9\xa7c6\x10c7\x01ae\x002b"[index]);
}

// Writes the decimal digits of a positive finite `value` to `buf` and returns true, or returns
// false when the digits need more than its first 64-bit segment (where fmt uses Dragon4).
// `precision` is the number of significant digits for scientific form, or of digits after the
// point when `fixed`; on return it is the number of digits written. The value is
// digits * 10^exp. `buf` needs room for precision + 1 digits.
inline bool format_float(double value, int& precision, bool fixed, char* buf, int& exp) noexcept {
  namespace db = eaux::text::static_format::detail::jkj::dragonbox;
  constexpr int num_significand_bits = 52;
  constexpr int exponent_bias = 1023;

  auto const br = std::bit_cast<std::uint64_t>(value);
  constexpr std::uint64_t significand_mask = (std::uint64_t(1) << num_significand_bits) - 1;
  std::uint64_t significand = br & significand_mask;
  int exponent = static_cast<int>((br >> num_significand_bits) & 0x7ff);

  if (exponent != 0) {  // Normal.
    exponent -= exponent_bias + num_significand_bits;
    significand |= std::uint64_t(1) << num_significand_bits;
    significand <<= 1;
  } else {  // Subnormal: normalize.
    int shift = std::countl_zero(significand);
    shift -= 64 - num_significand_bits - 2;
    exponent = (-1021 - num_significand_bits) - shift;
    significand <<= shift;
  }

  // Compute the first several nonzero decimal significand digits: the first segment.
  constexpr int kappa = 2;
  int const k = kappa - static_cast<int>(db::detail::log::floor_log10_pow2(exponent));
  exp = -k;
  int const beta = exponent + static_cast<int>(db::detail::log::floor_log2_pow10(k));
  std::uint64_t first_segment;
  bool has_more_segments;
  int digits_in_the_first_segment;
  {
    auto const r = db::detail::wuint::umul192_upper128(significand << beta, cached_power(k));
    first_segment = r.high();
    has_more_segments = r.low() != 0;
    // The first segment can have 18 ~ 19 digits.
    if (first_segment >= 1000000000000000000ULL) {
      digits_in_the_first_segment = 19;
    } else {
      // When it is of 18 digits, align it to 19 digits by adding a bogus zero at the end.
      digits_in_the_first_segment = 18;
      first_segment *= 10;
    }
  }

  // The actual number of decimal digits to print.
  if (fixed) precision += exp + digits_in_the_first_segment;

  if (digits_in_the_first_segment <= precision) return false;

  if (precision <= 0) {
    exp += digits_in_the_first_segment;
    if (precision < 0) {
      // Nothing to print but leading zeros.
      precision = 0;
    } else {
      // We may need to round up.
      buf[0] = (first_segment | static_cast<std::uint64_t>(has_more_segments)) > 5000000000000000000ULL ? '1' : '0';
      precision = 1;
    }
    return true;
  }

  exp += digits_in_the_first_segment - precision;

  // Divide the first segment into three subsegments of 9, 9 and 0 ~ 1 digits so that each fits
  // in 32 bits. 7922816251426433760 is ceil(2^(64+32) / 10^10).
  auto const first_subsegment =
      static_cast<std::uint32_t>(db::detail::wuint::umul128_upper64(first_segment, 7922816251426433760ULL) >> 32);
  std::uint64_t const second_third_subsegments = first_segment - first_subsegment * 10000000000ULL;

  std::uint64_t prod;
  std::uint32_t digits;
  bool should_round_up;
  int number_of_digits_to_print = precision < 9 ? precision : 9;

  // Prints a 9-digit subsegment, either the first or the second.
  auto print_subsegment = [&](std::uint32_t subsegment, char* buffer) {
    int number_of_digits_printed = 0;
    if ((number_of_digits_to_print & 1) != 0) {
      // 64-bit fixed-point fraction with a 1-digit integer part. 720575941 approximates
      // 2^(32 + 24) / 10^8.
      prod = ((subsegment * static_cast<std::uint64_t>(720575941)) >> 24) + 1;
      digits = static_cast<std::uint32_t>(prod >> 32);
      *buffer = static_cast<char>('0' + digits);
      number_of_digits_printed++;
    } else {
      // 64-bit fixed-point fraction with a 2-digit integer part. 450359963 approximates
      // 2^(32 + 20) / 10^7.
      prod = ((subsegment * static_cast<std::uint64_t>(450359963)) >> 20) + 1;
      digits = static_cast<std::uint32_t>(prod >> 32);
      write2digits(buffer, digits);
      number_of_digits_printed += 2;
    }
    while (number_of_digits_printed < number_of_digits_to_print) {
      prod = static_cast<std::uint32_t>(prod) * static_cast<std::uint64_t>(100);
      digits = static_cast<std::uint32_t>(prod >> 32);
      write2digits(buffer + number_of_digits_printed, digits);
      number_of_digits_printed += 2;
    }
  };

  print_subsegment(first_subsegment, buf);

  if (precision <= 9) {
    // Round up when the fractional part is above 1/2, or exactly 1/2 and the last digit is odd.
    if (precision < 9) {
      auto const fractional_part = static_cast<std::uint32_t>(prod);
      should_round_up = fractional_part >= fractional_part_rounding_thresholds(8 - number_of_digits_to_print) ||
                        ((fractional_part >> 31) & ((digits & 1) | (second_third_subsegments != 0) | has_more_segments)) != 0;
    } else {
      // Rounding at the subsegment boundary.
      should_round_up = second_third_subsegments > 5000000000ULL ||
                        (second_third_subsegments == 5000000000ULL && ((digits & 1) != 0 || has_more_segments));
    }
  } else {
    // 1844674407370955162 = ceil(2^64/10) < ceil(2^64*(10^9/(10^10 - 1))).
    auto const second_subsegment =
        static_cast<std::uint32_t>(db::detail::wuint::umul128_upper64(second_third_subsegments, 1844674407370955162ULL));
    std::uint32_t const third_subsegment = static_cast<std::uint32_t>(second_third_subsegments) - second_subsegment * 10;

    number_of_digits_to_print = precision - 9;
    print_subsegment(second_subsegment, buf + 9);

    if (precision < 18) {
      // third_subsegment != 0 implies a 19-digit segment, so it is a genuine digit.
      auto const fractional_part = static_cast<std::uint32_t>(prod);
      should_round_up = fractional_part >= fractional_part_rounding_thresholds(8 - number_of_digits_to_print) ||
                        ((fractional_part >> 31) & ((digits & 1) | (third_subsegment != 0) | has_more_segments)) != 0;
    } else {
      // The segment has 19 digits, so the third subsegment is a genuine digit.
      should_round_up = third_subsegment > 5 || (third_subsegment == 5 && ((digits & 1) != 0 || has_more_segments));
    }
  }

  if (should_round_up) {
    ++buf[precision - 1];
    for (int i = precision - 1; i > 0 && buf[i] > '9'; --i) {
      buf[i] = '0';
      ++buf[i - 1];
    }
    if (buf[0] > '9') {
      buf[0] = '1';
      if (fixed) {
        buf[precision++] = '0';
      } else {
        ++exp;
      }
    }
  }
  return true;
}

}  // namespace eaux::text::static_format::detail::fmt_port
