#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace eaux::text::static_format::detail {

enum class syntax : std::uint8_t { printf, brace };

enum class align : std::uint8_t { none, left, right, center, internal };

enum class sign : std::uint8_t { none, plus, space };

// Every way a format string can be rejected. Parse errors are reported where the literal is
// written; std_spec_* errors in a brace field are reported where an argument of a built-in type
// meets that field, since a user type may give the same text its own meaning.
enum class parse_error : std::uint8_t {
  none,
  printf_incomplete,
  printf_unknown_conversion,
  printf_wide_conversion,
  printf_n_conversion,
  printf_dynamic_width,
  printf_index_zero,
  printf_mixed_indexing,
  printf_unterminated_braces,
  printf_unknown_key,
  printf_ascii_needs_s,
  printf_user_type_flags,
  printf_conflicting_alignment,
  brace_unmatched_close,
  brace_unterminated,
  brace_mixed_indexing,
  brace_invalid_arg_id,
  brace_nested_field,
  std_spec_invalid,
  std_spec_locale,
  std_spec_debug,
};

inline constexpr std::size_t no_precision = std::numeric_limits<std::size_t>::max();

struct fill_char {
  char bytes[4] = {' ', 0, 0, 0};
  std::uint8_t size = 1;
};

// The parsed form of one replacement field, shared by both syntaxes. It is a structural type so
// the writers receive it as a template argument and fold everything about the field at compile
// time.
struct field_spec {
  syntax syn = syntax::printf;
  char conversion = 0;  // printf conversion letter, or brace presentation type (0 when absent)
  fill_char fill{};
  align alignment = align::none;
  sign sign_mode = sign::none;
  bool alternate = false;
  bool zero = false;
  std::size_t width = 0;  // 0: no width
  std::size_t precision = no_precision;
  bool ascii_only = false;  // %{ascii}s
  std::size_t raw_begin = 0;  // spec text in the source string: after ':' (brace) or inside {} (printf)
  std::size_t raw_size = 0;
  parse_error std_error = parse_error::none;  // brace only: the raw text read as a standard spec
};

enum class segment_kind : std::uint8_t { literal, argument };

struct segment {
  segment_kind kind = segment_kind::literal;
  std::size_t begin = 0;  // literal: offset into the unescaped text
  std::size_t size = 0;
  bool ascii = true;
  std::size_t arg = 0;  // argument: zero-based index
  field_spec spec{};
};

}  // namespace eaux::text::static_format::detail
