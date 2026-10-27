#pragma once

#include <eaux/text/static_format/detail/segment.hpp>
#include <eaux/text/static_format/detail/sink.hpp>
#include <eaux/text/unicode/unicode.hpp>

#include <algorithm>
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
// Numbers are converted with std::to_chars and are therefore always "C"-locale.
namespace eaux::text::static_format::detail {

template <class Sink>
void put_fill(Sink& sink, fill_char const& fill, std::size_t n) {
  if (fill.size == 1) {
    sink.fill(fill.bytes[0], n);
  } else {
    for (std::size_t i = 0; i < n; ++i) sink.put(std::string_view(fill.bytes, fill.size), charset::utf8);
  }
}

// Writes [prefix][zeros][body], padded to the field width. `prefix` (sign, base prefix) is
// ASCII; `body_width` is the display width of `body`.
template <field_spec Spec, align Default, class Sink>
void put_padded(Sink& sink, std::string_view prefix, std::size_t zeros, std::string_view body, charset kind,
                std::size_t body_width, bool zero_pad) {
  if constexpr (Spec.width == 0) {
    sink.put(prefix, charset::ascii);
    sink.fill('0', zeros);
    sink.put(body, kind);
  } else {
    std::size_t const content = prefix.size() + zeros + body_width;
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
    put_fill(sink, Spec.fill, before);
    sink.put(prefix, charset::ascii);
    put_fill(sink, Spec.fill, inside);
    sink.fill('0', zeros);
    sink.put(body, kind);
    put_fill(sink, Spec.fill, after);
  }
}

// printf right-aligns everything; std::format right-aligns numbers and left-aligns the rest.
template <field_spec Spec, bool Numeric>
inline constexpr align default_align = Spec.syn == syntax::printf || Numeric ? align::right : align::left;

template <field_spec Spec, charset Kind, class Sink>
void write_text(Sink& sink, std::string_view s) {
  constexpr align fallback = default_align<Spec, false>;
  if constexpr (Spec.width == 0 && Spec.precision == no_precision) {
    sink.put(s, Kind);
  } else if constexpr (Kind == charset::ascii) {
    // A copy: clang 21 miscompiles std::min given a reference into the template parameter object.
    constexpr std::size_t precision = Spec.precision;
    if constexpr (precision != no_precision) s = s.substr(0, std::min(precision, s.size()));
    put_padded<Spec, fallback>(sink, {}, 0, s, Kind, s.size(), false);
  } else if constexpr (Spec.precision != no_precision) {
    auto const fits = unicode::width_prefix(s, Spec.precision);
    put_padded<Spec, fallback>(sink, {}, 0, s.substr(0, fits.size), Kind, fits.width, false);
  } else {
    put_padded<Spec, fallback>(sink, {}, 0, s, Kind, unicode::display_width(s), false);
  }
}

template <field_spec Spec, class Sink, class C>
void write_char(Sink& sink, C c) {
  constexpr align fallback = default_align<Spec, false>;
  if constexpr (sizeof(C) == 1) {
    char const byte = static_cast<char>(c);
    put_padded<Spec, fallback>(sink, {}, 0, std::string_view(&byte, 1), charset::ascii, 1, false);
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
    put_padded<Spec, fallback>(sink, {}, 0, std::string_view(bytes, n), charset::utf8, unicode::code_point_width(cp), false);
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

  char prefix[3];
  std::size_t prefix_size = 0;
  // In printf only the signed conversions show + or space; std::format shows them in any base.
  constexpr bool shows_sign = !printf || conversion == 'd' || conversion == 'i';
  if (negative) {
    prefix[prefix_size++] = '-';
  } else if constexpr (shows_sign && Spec.sign_mode == sign::plus) {
    prefix[prefix_size++] = '+';
  } else if constexpr (shows_sign && Spec.sign_mode == sign::space) {
    prefix[prefix_size++] = ' ';
  }

  std::size_t zeros = 0;
  if constexpr (printf) {
    if constexpr (Spec.precision != no_precision) {
      if (Spec.precision == 0 && magnitude == 0) digits = {};
      if (digits.size() < Spec.precision) zeros = Spec.precision - digits.size();
    }
    if constexpr (Spec.alternate && base == 16) {
      if (magnitude != 0) prefix[prefix_size++] = '0', prefix[prefix_size++] = conversion;
    } else if constexpr (Spec.alternate && base == 8) {
      if (zeros == 0 && (digits.empty() || digits.front() != '0')) zeros = 1;
    }
  } else if constexpr (Spec.alternate && base != 10) {
    if constexpr (base == 8) {
      if (magnitude != 0) prefix[prefix_size++] = '0';
    } else {
      prefix[prefix_size++] = '0', prefix[prefix_size++] = conversion;
    }
  }

  constexpr bool zero_pad = printf ? Spec.zero && Spec.precision == no_precision &&
                                         (Spec.alignment == align::none || Spec.alignment == align::right ||
                                          Spec.alignment == align::internal)
                                   : Spec.zero && Spec.alignment == align::none;
  put_padded<Spec, align::right>(sink, std::string_view(prefix, prefix_size), zeros, digits, charset::ascii, digits.size(),
                                 zero_pad);
}

template <class F>
using to_chars_float = std::conditional_t<std::is_same_v<F, long double> && sizeof(long double) == sizeof(double), double, F>;

// Returns the length written, or 0 when the buffer is too small (a conversion is never empty).
template <class F>
std::size_t float_chars(char* first, std::size_t capacity, F value, std::chars_format format, std::size_t precision) {
  auto const v = static_cast<to_chars_float<F>>(value);
  auto const r = precision == no_precision ? std::to_chars(first, first + capacity, v, format)
                                           : std::to_chars(first, first + capacity, v, format, static_cast<int>(precision));
  return r.ec == std::errc{} ? static_cast<std::size_t>(r.ptr - first) : 0;
}

template <class F>
std::size_t shortest_chars(char* first, std::size_t capacity, F value) {
  auto const r = std::to_chars(first, first + capacity, static_cast<to_chars_float<F>>(value));
  return r.ec == std::errc{} ? static_cast<std::size_t>(r.ptr - first) : 0;
}

// The decimal exponent of a %e rendering, e.g. 3 for "1.234e+03".
inline int exponent_of(std::string_view sci) noexcept {
  auto const e = sci.find('e');
  int x = 0;
  std::from_chars(sci.data() + e + (sci[e + 1] == '+' ? 2 : 1), sci.data() + sci.size(), x);
  return x;
}

template <field_spec Spec, class Sink, std::floating_point F>
void write_float(Sink& sink, F value) {
  constexpr char conversion = Spec.conversion;
  constexpr bool printf = Spec.syn == syntax::printf;
  constexpr bool upper = conversion == 'E' || conversion == 'F' || conversion == 'G' || conversion == 'A';

  bool const negative = std::signbit(value);
  F const magnitude = negative ? -value : value;
  char prefix[3];
  std::size_t prefix_size = 0;
  if (negative) {
    prefix[prefix_size++] = '-';
  } else if constexpr (Spec.sign_mode == sign::plus) {
    prefix[prefix_size++] = '+';
  } else if constexpr (Spec.sign_mode == sign::space) {
    prefix[prefix_size++] = ' ';
  }

  if (!std::isfinite(value)) {
    std::string_view const body = std::isnan(value) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
    put_padded<Spec, align::right>(sink, std::string_view(prefix, prefix_size), 0, body, charset::ascii, body.size(), false);
    return;
  }

  constexpr auto format = conversion == 'f' || conversion == 'F'   ? std::chars_format::fixed
                          : conversion == 'e' || conversion == 'E' ? std::chars_format::scientific
                          : conversion == 'a' || conversion == 'A' ? std::chars_format::hex
                                                                   : std::chars_format::general;
  // printf defaults to precision 6 except for %a; std::format defaults to the shortest
  // round-trip form when the type is absent or a/A.
  constexpr bool shortest = Spec.precision == no_precision &&
                            (format == std::chars_format::hex || (!printf && conversion == 0));
  constexpr std::size_t precision = shortest                            ? no_precision
                                    : Spec.precision != no_precision ? Spec.precision
                                                                       : 6;
  if constexpr (printf && format == std::chars_format::hex) {
    prefix[prefix_size++] = '0', prefix[prefix_size++] = upper ? 'X' : 'x';
  }

  // Each attempt leaves one byte spare for the decimal point that # may insert.
  auto const render = [&](char* out, std::size_t capacity) -> std::size_t {
    std::size_t size;
    if constexpr (shortest && conversion == 0) {
      size = shortest_chars(out, capacity, magnitude);
    } else if constexpr (Spec.alternate && format == std::chars_format::general) {
      // %#g keeps trailing zeros, which to_chars' general format strips: pick %e or %f by C's
      // rule and render that with the matching precision.
      std::size_t const p = precision == 0 ? 1 : precision;
      size = float_chars(out, capacity, magnitude, std::chars_format::scientific, p - 1);
      if (size == 0) return 0;
      int const x = exponent_of(std::string_view(out, size));
      if (static_cast<long long>(p) > x && x >= -4) {
        size = float_chars(out, capacity, magnitude, std::chars_format::fixed,
                           static_cast<std::size_t>(static_cast<long long>(p) - 1 - x));
      }
    } else {
      size = float_chars(out, capacity, magnitude, format, precision);
    }
    if constexpr (Spec.alternate) {
      std::string_view const rendered(out, size);
      if (size != 0 && rendered.find('.') == std::string_view::npos) {
        auto const exponent = std::min(rendered.find(format == std::chars_format::hex ? 'p' : 'e'), size);
        std::copy_backward(out + exponent, out + size, out + size + 1);
        out[exponent] = '.';
        ++size;
      }
    }
    return size;
  };

  // Almost every conversion fits on the stack; only a large precision or a huge %f value
  // (up to max_exponent10 integral digits) needs the heap.
  char small[128];
  char* out = small;
  std::size_t size = render(small, sizeof small - 1);
  std::string large;
  if (size == 0) {
    large.resize((shortest ? 0 : precision) + std::numeric_limits<F>::max_exponent10 + 32);
    out = large.data();
    size = render(out, large.size() - 1);
  }
  if constexpr (upper) to_upper(out, out + size);

  constexpr bool zero_pad =
      printf ? Spec.zero && Spec.alignment != align::left && Spec.alignment != align::center : Spec.zero && Spec.alignment == align::none;
  put_padded<Spec, align::right>(sink, std::string_view(prefix, prefix_size), 0, std::string_view(out, size), charset::ascii, size,
                                 zero_pad);
}

template <field_spec Spec, class Sink>
void write_pointer(Sink& sink, std::uintptr_t address) {
  char buffer[sizeof address * 2];
  auto const end = std::to_chars(buffer, buffer + sizeof buffer, address, 16).ptr;
  std::string_view const digits(buffer, static_cast<std::size_t>(end - buffer));
  put_padded<Spec, align::right>(sink, "0x", 0, digits, charset::ascii, digits.size(), false);
}

}  // namespace eaux::text::static_format::detail
