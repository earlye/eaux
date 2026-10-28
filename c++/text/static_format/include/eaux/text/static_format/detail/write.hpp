#pragma once

#include <eaux/text/static_format/detail/float_chars.hpp>
#include <eaux/text/static_format/detail/segment.hpp>
#include <eaux/text/static_format/detail/sink.hpp>
#include <eaux/text/unicode/unicode.hpp>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

// Writers for the built-in argument kinds. Each takes its field_spec as a template argument, so
// the branches on flags, width and conversion fold away and only the conversion itself runs.
// Integers are converted with std::to_chars and floats by float_chars.hpp (Dragonbox and a port of
// fmt's digits), so both are always "C"-locale.
namespace eaux::text::static_format::detail {

// The fill is a template argument so a one-byte fill folds to sink.fill and inlines; with a
// runtime fill, g++ calls put_fill out of line for every padded field.
template <fill_char Fill, class Sink>
void put_fill(Sink& sink, std::size_t n) {
  if constexpr (Fill.size == 1) {
    sink.fill(Fill.bytes[0], n);
  } else {
    for (std::size_t i = 0; i < n; ++i) sink.put(std::string_view(Fill.bytes, Fill.size));
  }
}

// Writes [sign][base][zeros][body], padded to the field width. `sign` and `base` are ASCII
// literals; `body_width` is the display width of `body`.
template <field_spec Spec, align Default, class Sink>
void put_padded(Sink& sink, std::string_view sign, std::string_view base, std::size_t zeros, std::string_view body,
                std::size_t body_width, bool zero_pad) {
  if constexpr (Spec.width == 0) {
    sink.put(sign);
    sink.put(base);
    sink.fill('0', zeros);
    sink.put(body);
  } else {
    std::size_t const content = sign.size() + base.size() + zeros + body_width;
    std::size_t pad = Spec.width > content ? Spec.width - content : 0;
    if (zero_pad) zeros += pad, pad = 0;
    constexpr align alignment = Spec.alignment == align::none ? Default : Spec.alignment;
    std::size_t before = 0;
    std::size_t inside = 0;
    std::size_t after = 0;
    if constexpr (alignment == align::left) {
      after = pad;
    } else if constexpr (alignment == align::center) {
      before = pad / 2;
      after = pad - before;
    } else if constexpr (alignment == align::internal) {
      inside = pad;
    } else {
      before = pad;
    }
    put_fill<Spec.fill>(sink, before);
    sink.put(sign);
    sink.put(base);
    put_fill<Spec.fill>(sink, inside);
    sink.fill('0', zeros);
    sink.put(body);
    put_fill<Spec.fill>(sink, after);
  }
}

// In printf only the signed conversions show + or space; std::format shows them for any number.
template <field_spec Spec, bool ShowsSign = true>
constexpr std::string_view sign_of(bool negative) noexcept {
  if (negative) return "-";
  if constexpr (ShowsSign && Spec.sign_mode == sign::plus) {
    return "+";
  } else if constexpr (ShowsSign && Spec.sign_mode == sign::space) {
    return " ";
  } else {
    return {};
  }
}

template <char Conversion>
inline constexpr char base_prefix[2] = {'0', Conversion};

// printf right-aligns everything; std::format right-aligns numbers and left-aligns the rest.
template <field_spec Spec, bool Numeric>
inline constexpr align default_align = Spec.syn == syntax::printf || Numeric ? align::right : align::left;

// `Ascii` is known from the argument's type (ascii_string_view), which skips the scan for
// non-ASCII bytes.
template <field_spec Spec, bool Ascii, class Sink>
void write_text(Sink& sink, std::string_view s) {
  constexpr align fallback = default_align<Spec, false>;
  if constexpr (Spec.width == 0 && Spec.precision == no_precision) {
    sink.put(s);
  } else if constexpr (Ascii) {
    if constexpr (Spec.precision != no_precision) s = s.substr(0, unicode::ascii_cut(s, Spec.precision));
    put_padded<Spec, fallback>(sink, {}, {}, 0, s, s.size(), false);
  } else if constexpr (Spec.precision != no_precision) {
    auto const fits = unicode::width_prefix(s, Spec.precision);
    put_padded<Spec, fallback>(sink, {}, {}, 0, s.substr(0, fits.size), fits.width, false);
  } else {
    put_padded<Spec, fallback>(sink, {}, {}, 0, s, unicode::display_width(s), false);
  }
}

template <field_spec Spec, class Sink, class C>
void write_char(Sink& sink, C c) {
  constexpr align fallback = default_align<Spec, false>;
  if constexpr (sizeof(C) == 1) {
    char const byte = static_cast<char>(c);
    put_padded<Spec, fallback>(sink, {}, {}, 0, std::string_view(&byte, 1), 1, false);
  } else {
    auto cp = static_cast<char32_t>(c);
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = U'\uFFFD';
    char bytes[4];
    std::size_t n;
    auto const unit = [](char32_t v) { return static_cast<char>(static_cast<unsigned char>(v)); };
    if (cp < 0x80) {
      bytes[0] = unit(cp), n = 1;
    } else if (cp < 0x800) {
      bytes[0] = unit(0xC0 | (cp >> 6)), bytes[1] = unit(0x80 | (cp & 0x3F)), n = 2;
    } else if (cp < 0x10000) {
      bytes[0] = unit(0xE0 | (cp >> 12)), bytes[1] = unit(0x80 | ((cp >> 6) & 0x3F)), bytes[2] = unit(0x80 | (cp & 0x3F)), n = 3;
    } else {
      bytes[0] = unit(0xF0 | (cp >> 18)), bytes[1] = unit(0x80 | ((cp >> 12) & 0x3F));
      bytes[2] = unit(0x80 | ((cp >> 6) & 0x3F)), bytes[3] = unit(0x80 | (cp & 0x3F)), n = 4;
    }
    put_padded<Spec, fallback>(sink, {}, {}, 0, std::string_view(bytes, n), unicode::code_point_width(cp), false);
  }
}

constexpr void to_upper(char* first, char* last) noexcept {
  for (; first != last; ++first) {
    if (*first >= 'a' && *first <= 'z') *first = static_cast<char>(*first - 'a' + 'A');
  }
}

template <field_spec Spec, class Sink, std::integral T>
void write_integer(Sink& sink, T value) {
  constexpr char conversion = Spec.conversion;
  constexpr bool printf = Spec.syn == syntax::printf;
  // printf's unsigned conversions reinterpret negative values, as C does.
  constexpr bool reinterpret = printf && (conversion == 'u' || conversion == 'o' || conversion == 'x' || conversion == 'X');
  constexpr int base = conversion == 'x' || conversion == 'X' ? 16
                       : conversion == 'o'                     ? 8
                       : conversion == 'b' || conversion == 'B' ? 2
                                                                : 10;
  using U = std::make_unsigned_t<T>;

  bool negative = false;
  U magnitude = static_cast<U>(value);
  if constexpr (std::is_signed_v<T> && !reinterpret) {
    if (value < 0) {
      negative = true;
      magnitude = static_cast<U>(U{0} - magnitude);
    }
  }

  char buffer[sizeof(U) * 8];
  auto const end = std::to_chars(buffer, buffer + sizeof buffer, magnitude, base).ptr;
  if constexpr (conversion == 'X') to_upper(buffer, end);
  std::string_view digits(buffer, static_cast<std::size_t>(end - buffer));

  std::string_view const sign = sign_of<Spec, !printf || conversion == 'd' || conversion == 'i'>(negative);
  std::string_view prefix;
  std::size_t zeros = 0;
  if constexpr (printf) {
    if constexpr (Spec.precision != no_precision) {
      if (Spec.precision == 0 && magnitude == 0) digits = {};
      if (digits.size() < Spec.precision) zeros = Spec.precision - digits.size();
    }
    if constexpr (Spec.alternate && base == 16) {
      if (magnitude != 0) prefix = std::string_view(base_prefix<conversion>, 2);
    } else if constexpr (Spec.alternate && base == 8) {
      if (zeros == 0 && (digits.empty() || digits.front() != '0')) zeros = 1;
    }
  } else if constexpr (Spec.alternate && base == 8) {
    if (magnitude != 0) prefix = "0";
  } else if constexpr (Spec.alternate && base != 10) {
    prefix = std::string_view(base_prefix<conversion>, 2);
  }

  constexpr bool zero_pad = printf ? Spec.zero && Spec.precision == no_precision &&
                                         (Spec.alignment == align::none || Spec.alignment == align::right ||
                                          Spec.alignment == align::internal)
                                   : Spec.zero && Spec.alignment == align::none;
  put_padded<Spec, align::right>(sink, sign, prefix, zeros, digits, digits.size(), zero_pad);
}

// The decimal exponent of a %e rendering, e.g. 3 for "1.234e+03".
inline int exponent_of(std::string_view sci) noexcept {
  auto const e = sci.find('e');
  int x = 0;
  std::from_chars(sci.data() + e + (sci[e + 1] == '+' ? 2 : 1), sci.data() + sci.size(), x);
  return x;
}

// What a float field's spec fixes at compile time.
template <field_spec Spec>
struct float_form {
  static constexpr char conversion = Spec.conversion;
  static constexpr bool printf = Spec.syn == syntax::printf;
  static constexpr bool upper = conversion == 'E' || conversion == 'F' || conversion == 'G' || conversion == 'A';
  static constexpr auto format = conversion == 'f' || conversion == 'F'   ? std::chars_format::fixed
                                 : conversion == 'e' || conversion == 'E' ? std::chars_format::scientific
                                 : conversion == 'a' || conversion == 'A' ? std::chars_format::hex
                                                                          : std::chars_format::general;
  // printf defaults to precision 6 except for %a; std::format defaults to the shortest
  // round-trip form when the type is absent or a/A.
  static constexpr bool shortest = Spec.precision == no_precision &&
                                   (format == std::chars_format::hex || (!printf && conversion == 0));
  static constexpr std::size_t precision = shortest                            ? no_precision
                                           : Spec.precision != no_precision ? Spec.precision
                                                                              : 6;
  static constexpr std::string_view prefix = printf && format == std::chars_format::hex ? (upper ? "0X" : "0x") : "";
  static constexpr bool zero_pad = printf ? Spec.zero && Spec.alignment != align::left && Spec.alignment != align::center
                                          : Spec.zero && Spec.alignment == align::none;
};

// Renders a non-negative finite value; returns the length, or 0 when `capacity` is too small.
// `capacity` must be one less than the buffer, so # can insert a decimal point.
template <field_spec Spec, std::floating_point F>
std::size_t render_float(char* out, std::size_t capacity, F magnitude) {
  using form = float_form<Spec>;
  std::size_t size;
  if constexpr (form::shortest && form::conversion == 0) {
    size = shortest_chars(out, capacity, magnitude);
  } else if constexpr (Spec.alternate && form::format == std::chars_format::general) {
    // %#g keeps trailing zeros, which to_chars' general format strips: pick %e or %f by C's
    // rule and render that with the matching precision.
    std::size_t const p = form::precision == 0 ? 1 : form::precision;
    size = float_chars(out, capacity, magnitude, std::chars_format::scientific, p - 1);
    if (size == 0) return 0;
    int const x = exponent_of(std::string_view(out, size));
    if (static_cast<long long>(p) > x && x >= -4) {
      size = float_chars(out, capacity, magnitude, std::chars_format::fixed,
                         static_cast<std::size_t>(static_cast<long long>(p) - 1 - x));
    }
  } else {
    size = float_chars(out, capacity, magnitude, form::format, form::precision);
  }
  if constexpr (Spec.alternate) {
    std::string_view const rendered(out, size);
    if (size != 0 && rendered.find('.') == std::string_view::npos) {
      auto const exponent = std::min(rendered.find(form::format == std::chars_format::hex ? 'p' : 'e'), size);
      std::copy_backward(out + exponent, out + size, out + size + 1);
      out[exponent] = '.';
      ++size;
    }
  }
  return size;
}

template <field_spec Spec, class Sink>
void finish_float(Sink& sink, std::string_view sign, char* out, std::size_t size) {
  using form = float_form<Spec>;
  if constexpr (form::upper) to_upper(out, out + size);
  put_padded<Spec, align::right>(sink, sign, form::prefix, 0, std::string_view(out, size), size, form::zero_pad);
}

// Only a large precision or a huge %f value (up to max_exponent10 integral digits) overflows the
// stack buffer. Out of line, so the std::string and its unwinding stay out of write_float's frame.
// The size must cover precision_chars' capacity check (integral digits + precision + 8).
template <field_spec Spec, class Sink, std::floating_point F>
EAUX_STATIC_FORMAT_NOINLINE void write_float_large(Sink& sink, std::string_view sign, F magnitude) {
  using form = float_form<Spec>;
  std::string large((form::shortest ? 0 : form::precision) + std::numeric_limits<F>::max_exponent10 + 32, '\0');
  std::size_t const size = render_float<Spec>(large.data(), large.size() - 1, magnitude);
  assert(size != 0 && "the heap buffer holds any conversion: precision plus max_exponent10 integral digits");
  finish_float<Spec>(sink, sign, large.data(), size);
}

template <field_spec Spec, class Sink, std::floating_point F>
EAUX_STATIC_FORMAT_ALWAYS_INLINE void write_float(Sink& sink, F value) {
  using form = float_form<Spec>;
  bool const negative = std::signbit(value);
  F const magnitude = negative ? -value : value;
  std::string_view const sign = sign_of<Spec>(negative);

  if (!std::isfinite(value)) {
    std::string_view const body = std::isnan(value) ? (form::upper ? "NAN" : "nan") : (form::upper ? "INF" : "inf");
    put_padded<Spec, align::right>(sink, sign, {}, 0, body, body.size(), false);
    return;
  }

  constexpr std::size_t small_size = 128;
  // A constant precision this large always fails precision_chars' capacity check, which needs
  // precision + 1 + precision_chars_overhead (%#g first tries %e at precision - 1), so skip the
  // stack attempt; MSVC otherwise warns (C4789) that zero_chars overruns `small`. %a, a wider
  // long double and precisions past max_fast_precision go to std::to_chars, where %g can fit.
  if constexpr (form::format != std::chars_format::hex && float_layout::fast<F> &&
                form::precision <= max_fast_precision &&
                form::precision + precision_chars_overhead > small_size - 1) {
    write_float_large<Spec>(sink, sign, magnitude);
  } else {
    char small[small_size];
    if (std::size_t const size = render_float<Spec>(small, sizeof small - 1, magnitude); size != 0) [[likely]] {
      finish_float<Spec>(sink, sign, small, size);
    } else {
      write_float_large<Spec>(sink, sign, magnitude);
    }
  }
}

template <field_spec Spec, class Sink>
void write_pointer(Sink& sink, std::uintptr_t address) {
  char buffer[sizeof address * 2];
  auto const end = std::to_chars(buffer, buffer + sizeof buffer, address, 16).ptr;
  std::string_view const digits(buffer, static_cast<std::size_t>(end - buffer));
  put_padded<Spec, align::right>(sink, {}, "0x", 0, digits, digits.size(), false);
}

}  // namespace eaux::text::static_format::detail
