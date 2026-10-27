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

// Non-recursive pack indexing, so dozens of arguments cost no template depth.
template <std::size_t I, class T>
struct indexed {
  using type = T;
};
template <class Sequence, class... T>
struct index_table;
template <std::size_t... I, class... T>
struct index_table<std::index_sequence<I...>, T...> : indexed<I, T>... {};
template <std::size_t I, class T>
indexed<I, T> select(indexed<I, T> const&);
template <std::size_t I, class... T>
using type_at = typename decltype(select<I>(std::declval<index_table<std::index_sequence_for<T...>, T...> const&>()))::type;

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

template <class T>
concept static_formattable = requires(std::string_view text) { formatter<std::remove_cv_t<T>>::parse(text); };

// Disabled std::formatter specializations are not default constructible ([formatter.requirements]).
template <class T>
concept std_formattable = std::is_default_constructible_v<std::formatter<std::remove_cv_t<T>, char>>;

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
