#pragma once

#include <eaux/text/static_format/detail/segment.hpp>
#include <eaux/text/static_format/detail/types.hpp>

#include <concepts>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

// Each check is a function template whose template arguments carry the facts of the error (the
// offset in the format string, the argument's index, the conversion, the argument's type), so
// the compiler's "in instantiation of" note names them next to the static_assert's message.
// Checks return whether they passed so callers can skip the code that would otherwise pile
// further errors on top.
namespace eaux::text::static_format::detail {

template <parse_error Error, std::size_t Offset>
constexpr bool format_string_error() {
  using enum parse_error;
  static_assert(Error != printf_incomplete, "static_format: '%' at this offset has no conversion specifier");
  static_assert(Error != printf_unknown_conversion, "static_format: unknown printf conversion specifier");
  static_assert(Error != printf_wide_conversion,
                "static_format: %C and %S are deprecated wide-character forms; use %c or %s");
  static_assert(Error != printf_n_conversion, "static_format: %n is not supported");
  static_assert(Error != printf_dynamic_width, "static_format: '*' (width or precision from an argument) is not supported");
  static_assert(Error != printf_index_zero, "static_format: positional arguments count from 1 (%1$, %1%)");
  static_assert(Error != printf_mixed_indexing,
                "static_format: a format string cannot mix positional (%N$, %N%) and sequential arguments");
  static_assert(Error != printf_unterminated_braces, "static_format: '{' in a printf conversion has no closing '}'");
  static_assert(Error != printf_unknown_key, "static_format: unknown key in a printf conversion's braced spec");
  static_assert(Error != printf_ascii_needs_s, "static_format: the {ascii} key applies only to %s");
  static_assert(Error != printf_user_type_flags,
                "static_format: %v and %V take only width, precision and the alignment flags - ^ >");
  static_assert(Error != printf_conflicting_alignment, "static_format: conflicting alignment flags (- ^ = >)");
  static_assert(Error != number_too_large, "static_format: width, precision or argument index above 1000000");
  static_assert(Error != brace_unmatched_close, "static_format: unmatched '}' in format string (write '}}' for a literal brace)");
  static_assert(Error != brace_unterminated, "static_format: replacement field has no closing '}'");
  static_assert(Error != brace_mixed_indexing,
                "static_format: a format string cannot mix automatic ({}) and manual ({0}) argument indexing");
  static_assert(Error != brace_invalid_arg_id, "static_format: invalid argument id in replacement field");
  static_assert(Error != brace_nested_field,
                "static_format: nested replacement fields (dynamic width or precision) are not supported");
  return Error == none;
}

template <syntax Syn, std::size_t Required, std::size_t Given>
constexpr bool argument_count() {
  static_assert(Given >= Required, "static_format: the format string uses more arguments than were passed");
  static_assert(Syn != syntax::printf || Given <= Required,
                "static_format: more arguments were passed than the printf format string uses");
  return Given >= Required && (Syn != syntax::printf || Given <= Required);
}

template <class T>
constexpr bool static_formatter_members() {
  using F = formatter<std::remove_cv_t<T>>;
  constexpr bool has_parse = requires(std::string_view text) { F::parse(text); };
  static_assert(has_parse, "static_format: formatter<T> needs a static constexpr parse(std::string_view) returning its spec");
  if constexpr (!has_parse) {
    return false;
  } else {
    using spec = decltype(F::parse(std::string_view{}));
    constexpr bool has_format = requires(spec const& s, std::back_insert_iterator<std::string> out, T const& value) {
      { F::format(s, out, value) } -> std::convertible_to<std::back_insert_iterator<std::string>>;
    };
    static_assert(has_format,
                  "static_format: formatter<T> needs a static format(spec, out, value) that writes through the output "
                  "iterator `out` and returns it");
    return has_format;
  }
}

template <std::size_t ArgNumber, char Conversion, bool AsciiOnly, class T>
constexpr bool printf_argument() {
  if constexpr (Conversion == 'd' || Conversion == 'i') {
    static_assert(printf_signed<T>, "static_format: %d and %i need an integral argument other than bool or a character type (char, wchar_t, char8/16/32_t)");
    return printf_signed<T>;
  } else if constexpr (Conversion == 'u' || Conversion == 'o' || Conversion == 'x' || Conversion == 'X') {
    static_assert(std::integral<T>, "static_format: %u, %o, %x and %X need an integral argument");
    return std::integral<T>;
  } else if constexpr (Conversion == 'c') {
    static_assert(printf_char<T>, "static_format: %c needs a character type (cast an int to char)");
    return printf_char<T>;
  } else if constexpr (Conversion == 's' && AsciiOnly) {
    constexpr bool ok = std::same_as<std::remove_cv_t<T>, ascii_string_view>;
    static_assert(ok, "static_format: %{ascii}s needs an eaux::text::ascii_string_view argument");
    return ok;
  } else if constexpr (Conversion == 's') {
    static_assert(string_like<T>,
                  "static_format: %s needs a string: char const*, std::string, std::string_view or ascii_string_view");
    return string_like<T>;
  } else if constexpr (Conversion == 'p') {
    constexpr bool ok = std::is_pointer_v<T> || std::is_null_pointer_v<T>;
    static_assert(ok, "static_format: %p needs a pointer");
    return ok;
  } else if constexpr (Conversion == 'v') {
    constexpr bool ok = std_formattable<T> || streamable<T>;
    static_assert(ok, "static_format: %v needs a std::formatter<T> specialization or operator<<(std::ostream&, T const&)");
    return ok;
  } else if constexpr (Conversion == 'V') {
    static_assert(static_formattable<T>, "static_format: %V needs a specialization of eaux::text::static_format::formatter<T>");
    if constexpr (static_formattable<T>) {
      return static_formatter_members<T>();
    } else {
      return false;
    }
  } else {
    static_assert(std::floating_point<T>, "static_format: %f, %e, %g and %a need a floating-point argument");
    return std::floating_point<T>;
  }
}

template <std::size_t ArgId, char Type, class T, parse_error SpecError, bool Arithmetic, bool Precision>
constexpr bool brace_builtin_argument() {
  using enum parse_error;
  static_assert(SpecError != std_spec_invalid, "static_format: invalid format spec for a built-in type");
  static_assert(SpecError != number_too_large, "static_format: width, precision or argument index above 1000000");
  static_assert(SpecError != std_spec_locale, "static_format: the L (locale) option is not supported");
  static_assert(SpecError != std_spec_debug, "static_format: the ? (debug) presentation type is not supported");
  if constexpr (SpecError != none) {
    return false;
  } else {
    constexpr auto allowed = []() -> std::string_view {
      if constexpr (boolean<T>) return "sbBdoxX";
      else if constexpr (brace_char<T>) return "cbBdoxX";
      else if constexpr (brace_integer<T>) return "bBcdoxX";
      else if constexpr (std::floating_point<T>) return "aAeEfFgG";
      else if constexpr (brace_pointer<T>) return "p";
      else return "s";
    }();
    constexpr bool type_ok = Type == 0 || allowed.find(Type) != std::string_view::npos;
    static_assert(type_ok, "static_format: presentation type not valid for this argument's type");
    // Sign, # and 0 need an arithmetic presentation; precision only means something for
    // floating point and strings.
    constexpr bool numeric = std::floating_point<T> || brace_integer<T> ||
                             ((brace_char<T> || boolean<T>) && Type != 0 && Type != 'c' && Type != 's');
    static_assert(!Arithmetic || numeric, "static_format: sign, # and 0 apply only to numbers");
    constexpr bool precision_ok = std::floating_point<T> || string_like<T>;
    static_assert(!Precision || precision_ok, "static_format: precision applies only to floating-point and string arguments");
    return type_ok && (!Arithmetic || numeric) && (!Precision || precision_ok);
  }
}

template <std::size_t ArgId, class T, bool HasSpec>
constexpr bool brace_user_argument() {
  if constexpr (static_formattable<T>) {
    return static_formatter_members<T>();
  } else {
    constexpr bool any = std_formattable<T> || streamable<T>;
    static_assert(any,
                  "static_format: no formatter for this type: specialize eaux::text::static_format::formatter<T> or "
                  "std::formatter<T>, or provide operator<<");
    constexpr bool spec_ok = !HasSpec || std_formattable<T>;
    static_assert(!any || spec_ok, "static_format: a type formatted through operator<< takes no format spec");
    return any && spec_ok;
  }
}

}  // namespace eaux::text::static_format::detail
