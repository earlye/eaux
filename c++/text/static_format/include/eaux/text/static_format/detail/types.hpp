#pragma once

#include <eaux/text/ascii_string_view.hpp>

#include <concepts>
#include <cstddef>
#include <format>
#include <iterator>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace eaux::text::static_format {

// Specialize for a user type to make it formattable with %V (printf) and {:spec} (brace):
//
//   template <> struct eaux::text::static_format::formatter<money> {
//     struct spec { int decimals; };                        // a structural type
//     static constexpr spec parse(std::string_view text);   // throw to reject: fails the build
//     template <class Out> static Out format(spec const&, Out out, money const&);
//   };
//
// parse runs at compile time on the text between the braces of %{text}V or after the colon of
// {:text}; format runs at run time and writes chars through `out`.
template <class T>
struct formatter {
  formatter() = delete;
};

namespace detail {

// The arguments of one call, each reachable by index without recursion and without naming its
// type through an alias: pick<J> deduces T from the matching base, so diagnostics spell the
// argument's real type (MSVC prints an alias by the alias's own name).
template <std::size_t J, class T>
struct slot {
  T const& value;
};
template <class Sequence, class... T>
struct argument_pack;
template <std::size_t... J, class... T>
struct argument_pack<std::index_sequence<J...>, T...> : slot<J, T>... {};
template <std::size_t J, class T>
constexpr T const& pick(slot<J, T> const& s) noexcept {
  return s.value;
}

template <class T, class... U>
concept one_of = (std::same_as<T, U> || ...);

// The types that hold characters rather than small numbers. %d and %i refuse them; signed and
// unsigned char stay integers there because they are int8_t and uint8_t.
template <class T>
concept character = one_of<std::remove_cv_t<T>, char, char8_t, char16_t, char32_t, wchar_t>;

// What %c accepts.
template <class T>
concept printf_char = character<T> || one_of<std::remove_cv_t<T>, signed char, unsigned char>;

template <class T>
concept printf_signed = std::integral<T> && !character<T> && !std::same_as<std::remove_cv_t<T>, bool>;

template <class It>
concept char_contiguous = std::contiguous_iterator<It> && std::same_as<std::iter_value_t<It>, char>;

template <class T>
concept char_array = std::is_array_v<T> && std::same_as<std::remove_cv_t<std::remove_extent_t<T>>, char>;

template <class T>
concept string_like = char_array<T> || one_of<std::remove_cv_t<T>, char const*, char*, std::string, std::string_view, ascii_string_view>;

// std::format's categories: only `char` is a character; signed and unsigned char are integers;
// the other character types are not formattable into char output.
template <class T>
concept brace_char = std::same_as<std::remove_cv_t<T>, char>;

template <class T>
concept brace_integer = printf_signed<T>;

template <class T>
concept brace_pointer = one_of<std::remove_cv_t<T>, void*, void const*, std::nullptr_t>;

template <class T>
concept boolean = std::same_as<std::remove_cv_t<T>, bool>;

template <class T>
concept brace_builtin = brace_integer<T> || brace_char<T> || boolean<T> || std::floating_point<T> || string_like<T> ||
                        brace_pointer<T>;

// Specialized at all: the primary template's deleted constructor marks it unspecialized.
// diagnostics.hpp checks that a specialization has the members it needs.
template <class T>
concept static_formattable = std::is_default_constructible_v<formatter<std::remove_cv_t<T>>>;

// std::formattable<T, char> ([format.formattable]), spelled out because libstdc++ only provides
// it from GCC 15. As the standard does, it requires format() to accept a context over an
// iterator other than std::format_context's: libc++ rejects a formatter whose format() takes
// std::format_context& specifically, so such a formatter must not count as usable here.
template <class T, class F = std::formatter<std::remove_cv_t<T>, char>>
concept std_formattable =
    std::semiregular<F> &&
    requires(F& f, F const& cf, T const& value, std::format_parse_context& parse,
             std::basic_format_context<std::back_insert_iterator<std::string>, char>& format) {
      { f.parse(parse) } -> std::same_as<std::format_parse_context::iterator>;
      { cf.format(value, format) } -> std::same_as<std::back_insert_iterator<std::string>>;
    };

template <class T>
concept streamable = requires(std::ostream& os, T const& value) { os << value; };

template <class T>
constexpr std::string_view as_string_view(T const& value) noexcept {
  if constexpr (char_array<T>) {
    std::size_t n = 0;
    while (n < std::extent_v<T> && value[n] != '\0') ++n;
    return {value, n};
  } else if constexpr (std::is_pointer_v<T>) {
    return value ? std::string_view(value) : std::string_view("(null)");
  } else {
    return std::string_view(value);
  }
}

}  // namespace detail
}  // namespace eaux::text::static_format
