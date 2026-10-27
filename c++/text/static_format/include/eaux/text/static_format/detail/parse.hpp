#pragma once

#include <eaux/text/static_format/detail/segment.hpp>
#include <eaux/text/unicode/unicode.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace eaux::text::static_format::detail {

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

// Reads decimal digits at s[i...]; a value too large for any sane width saturates, which still
// pads absurdly but cannot overflow.
constexpr std::size_t read_number(std::string_view s, std::size_t& i) noexcept {
  std::size_t value = 0;
  for (; i < s.size() && is_digit(s[i]); ++i) {
    value = value > 100'000'000 ? value : value * 10 + static_cast<std::size_t>(s[i] - '0');
  }
  return value;
}

// The standard format spec, [format.string.std]:
//   [[fill]align][sign][#][0][width][.precision][type]
// Nested replacement fields and L are rejected (deferred), as is the ? debug type.
constexpr parse_error parse_std_spec(std::string_view s, field_spec& spec) noexcept {
  auto const align_of = [](char c) {
    switch (c) {
      case '<': return align::left;
      case '>': return align::right;
      case '^': return align::center;
      default: return align::none;
    }
  };
  std::size_t i = 0;
  if (!s.empty()) {
    auto const fill = unicode::decode(s, 0);
    if (fill.length < s.size() && align_of(s[fill.length]) != align::none) {
      if (!fill.valid) return parse_error::std_spec_invalid;
      for (std::size_t k = 0; k < fill.length; ++k) spec.fill.bytes[k] = s[k];
      spec.fill.size = static_cast<std::uint8_t>(fill.length);
      spec.alignment = align_of(s[fill.length]);
      i = fill.length + 1;
    } else if (align_of(s[0]) != align::none) {
      spec.alignment = align_of(s[0]);
      i = 1;
    }
  }
  if (i < s.size() && (s[i] == '+' || s[i] == '-' || s[i] == ' ')) {
    spec.sign_mode = s[i] == '+' ? sign::plus : s[i] == ' ' ? sign::space : sign::none;
    ++i;
  }
  if (i < s.size() && s[i] == '#') spec.alternate = true, ++i;
  if (i < s.size() && s[i] == '0') spec.zero = true, ++i;
  if (i < s.size() && is_digit(s[i])) spec.width = read_number(s, i);
  if (i < s.size() && s[i] == '.') {
    ++i;
    if (i == s.size() || !is_digit(s[i])) return parse_error::std_spec_invalid;
    spec.precision = read_number(s, i);
  }
  if (i < s.size() && s[i] == 'L') return parse_error::std_spec_locale;
  if (i < s.size()) {
    if (s[i] == '?') return parse_error::std_spec_debug;
    if (std::string_view("aAbBcdeEfFgGopsxX").find(s[i]) == std::string_view::npos) return parse_error::std_spec_invalid;
    spec.conversion = s[i++];
  }
  return i == s.size() ? parse_error::none : parse_error::std_spec_invalid;
}

struct parse_result {
  parse_error error = parse_error::none;
  std::size_t offset = 0;      // where the offending field starts
  std::size_t arguments = 0;   // one past the highest argument index used
  std::size_t segments = 0;
  std::size_t text_size = 0;   // unescaped literal bytes
};

// The parsers report literal bytes and fields to a visitor. Counting and building share them so
// the segment array can be sized exactly.
struct counting_visitor {
  std::size_t segments = 0;
  std::size_t text = 0;
  bool open = false;

  constexpr void literal(char) noexcept {
    if (!open) ++segments, open = true;
    ++text;
  }
  constexpr void field(segment const&) noexcept { ++segments, open = false; }
};

template <std::size_t TextSize, std::size_t Count>
struct parsed_format {
  char text[TextSize + 1]{};
  std::array<segment, Count> segments{};
};

template <std::size_t TextSize, std::size_t Count>
struct building_visitor {
  parsed_format<TextSize, Count> out{};
  std::size_t text = 0;
  std::size_t next = 0;
  bool open = false;

  constexpr void literal(char c) noexcept {
    if (!open) {
      out.segments[next++] = segment{segment_kind::literal, text, 0, true, 0, {}};
      open = true;
    }
    auto& seg = out.segments[next - 1];
    out.text[text++] = c;
    ++seg.size;
    if (static_cast<unsigned char>(c) >= 0x80) seg.ascii = false;
  }
  constexpr void field(segment const& s) noexcept {
    out.segments[next++] = s;
    open = false;
  }
};

// printf: %[N$][flags][width][.precision][length][{spec}]conversion, plus %% and boost's %N%.
template <class Visitor>
constexpr parse_result parse_printf(std::string_view s, Visitor& visitor) noexcept {
  parse_result result;
  std::size_t next_sequential = 0;
  bool sequential = false;
  bool positional = false;
  auto const fail = [&](parse_error e, std::size_t at) {
    result.error = e;
    result.offset = at;
    return result;
  };
  auto const skip_length = [&](std::size_t& i) {
    while (i < s.size() && std::string_view("hlzjtL").find(s[i]) != std::string_view::npos) ++i;
  };

  for (std::size_t i = 0; i < s.size();) {
    if (s[i] != '%') {
      visitor.literal(s[i++]);
      continue;
    }
    std::size_t const start = i++;
    if (i == s.size()) return fail(parse_error::printf_incomplete, start);
    if (s[i] == '%') {
      visitor.literal('%');
      ++i;
      continue;
    }

    segment field{segment_kind::argument, 0, 0, true, 0, {}};
    field.spec.syn = syntax::printf;
    std::size_t index = 0;  // 1-based when positional
    std::size_t digits_end = i;
    std::size_t const number = read_number(s, digits_end);
    if (digits_end > i && digits_end < s.size() && (s[digits_end] == '%' || s[digits_end] == '$')) {
      if (number == 0) return fail(parse_error::printf_index_zero, start);
      index = number;
      bool const boost = s[digits_end] == '%';
      i = digits_end + 1;
      if (boost) {
        // %N% formats the argument the way {N-1} would.
        field.spec.syn = syntax::brace;
        field.arg = index - 1;
        positional = true;
        if (sequential) return fail(parse_error::printf_mixed_indexing, start);
        if (field.arg + 1 > result.arguments) result.arguments = field.arg + 1;
        visitor.field(field);
        continue;
      }
    }

    auto& spec = field.spec;
    for (bool flags = true; flags && i < s.size();) {
      align requested = align::none;
      switch (s[i]) {
        case '-': requested = align::left; break;
        case '>': requested = align::right; break;
        case '^': requested = align::center; break;
        case '=': requested = align::internal; break;
        case '+': spec.sign_mode = sign::plus; break;
        case ' ':
          if (spec.sign_mode == sign::none) spec.sign_mode = sign::space;
          break;
        case '#': spec.alternate = true; break;
        case '0': spec.zero = true; break;
        default: flags = false; continue;
      }
      if (requested != align::none) {
        if (spec.alignment != align::none && spec.alignment != requested) {
          return fail(parse_error::printf_conflicting_alignment, start);
        }
        spec.alignment = requested;
      }
      ++i;
    }
    if (i < s.size() && s[i] == '*') return fail(parse_error::printf_dynamic_width, start);
    spec.width = read_number(s, i);
    if (i < s.size() && s[i] == '.') {
      ++i;
      if (i < s.size() && s[i] == '*') return fail(parse_error::printf_dynamic_width, start);
      spec.precision = read_number(s, i);
    }
    skip_length(i);
    bool braced = false;
    if (i < s.size() && s[i] == '{') {
      braced = true;
      spec.raw_begin = ++i;
      while (i < s.size() && s[i] != '}' && s[i] != '{') ++i;
      if (i == s.size() || s[i] != '}') return fail(parse_error::printf_unterminated_braces, start);
      spec.raw_size = i++ - spec.raw_begin;
      skip_length(i);
    }
    if (i == s.size()) return fail(parse_error::printf_incomplete, start);

    char const conversion = s[i++];
    switch (conversion) {
      case 'd': case 'i': case 'u': case 'o': case 'x': case 'X':
      case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
      case 'c': case 's': case 'p': case 'v': case 'V':
        break;
      case 'C': case 'S': return fail(parse_error::printf_wide_conversion, start);
      case 'n': return fail(parse_error::printf_n_conversion, start);
      default: return fail(parse_error::printf_unknown_conversion, start);
    }
    spec.conversion = conversion;

    if (braced && conversion != 'V') {
      auto const key = s.substr(spec.raw_begin, spec.raw_size);
      if (key == "ascii") {
        if (conversion != 's') return fail(parse_error::printf_ascii_needs_s, start);
        spec.ascii_only = true;
      } else if (!key.empty()) {
        return fail(parse_error::printf_unknown_key, start);
      }
    }
    if ((conversion == 'v' || conversion == 'V') &&
        (spec.sign_mode != sign::none || spec.alternate || spec.zero || spec.alignment == align::internal)) {
      return fail(parse_error::printf_user_type_flags, start);
    }

    if (index != 0) {
      positional = true;
      field.arg = index - 1;
    } else {
      sequential = true;
      field.arg = next_sequential++;
    }
    if (positional && sequential) return fail(parse_error::printf_mixed_indexing, start);
    if (field.arg + 1 > result.arguments) result.arguments = field.arg + 1;
    visitor.field(field);
  }
  return result;
}

// std::format's replacement fields: {[arg-id][:spec]} with {{ and }} escapes.
template <class Visitor>
constexpr parse_result parse_brace(std::string_view s, Visitor& visitor) noexcept {
  parse_result result;
  std::size_t next_automatic = 0;
  bool automatic = false;
  bool manual = false;
  auto const fail = [&](parse_error e, std::size_t at) {
    result.error = e;
    result.offset = at;
    return result;
  };

  for (std::size_t i = 0; i < s.size();) {
    char const c = s[i];
    if (c == '}') {
      if (i + 1 < s.size() && s[i + 1] == '}') {
        visitor.literal('}');
        i += 2;
        continue;
      }
      return fail(parse_error::brace_unmatched_close, i);
    }
    if (c != '{') {
      visitor.literal(c);
      ++i;
      continue;
    }
    if (i + 1 < s.size() && s[i + 1] == '{') {
      visitor.literal('{');
      i += 2;
      continue;
    }

    std::size_t const start = i++;
    segment field{segment_kind::argument, 0, 0, true, 0, {}};
    field.spec.syn = syntax::brace;
    if (i == s.size()) return fail(parse_error::brace_unterminated, start);
    if (is_digit(s[i])) {
      if (s[i] == '0' && i + 1 < s.size() && is_digit(s[i + 1])) return fail(parse_error::brace_invalid_arg_id, start);
      field.arg = read_number(s, i);
      manual = true;
    } else {
      field.arg = next_automatic++;
      automatic = true;
    }
    if (manual && automatic) return fail(parse_error::brace_mixed_indexing, start);
    if (i == s.size()) return fail(parse_error::brace_unterminated, start);
    if (s[i] == ':') {
      field.spec.raw_begin = ++i;
      while (i < s.size() && s[i] != '}') {
        if (s[i] == '{') return fail(parse_error::brace_nested_field, start);
        ++i;
      }
      if (i == s.size()) return fail(parse_error::brace_unterminated, start);
      field.spec.raw_size = i - field.spec.raw_begin;
    } else if (s[i] != '}') {
      return fail(parse_error::brace_invalid_arg_id, start);
    }
    ++i;
    field.spec.std_error = parse_std_spec(s.substr(field.spec.raw_begin, field.spec.raw_size), field.spec);
    if (field.arg + 1 > result.arguments) result.arguments = field.arg + 1;
    visitor.field(field);
  }
  return result;
}

template <syntax Syn, class Visitor>
constexpr parse_result parse(std::string_view s, Visitor& visitor) noexcept {
  if constexpr (Syn == syntax::printf) {
    return parse_printf(s, visitor);
  } else {
    return parse_brace(s, visitor);
  }
}

template <syntax Syn>
constexpr parse_result scan(std::string_view s) noexcept {
  counting_visitor counter;
  auto result = parse<Syn>(s, counter);
  result.segments = counter.segments;
  result.text_size = counter.text;
  return result;
}

template <syntax Syn, std::size_t TextSize, std::size_t Count>
constexpr parsed_format<TextSize, Count> build(std::string_view s) noexcept {
  building_visitor<TextSize, Count> builder;
  parse<Syn>(s, builder);
  return builder.out;
}

}  // namespace eaux::text::static_format::detail
