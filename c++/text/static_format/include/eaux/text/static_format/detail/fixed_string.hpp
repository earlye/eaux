#pragma once

#include <cstddef>
#include <string_view>

namespace eaux::text::static_format {

// A string literal usable as a template argument, so the format string becomes part of the type
// and is parsed at compile time. N counts the terminating NUL and is deduced, so there is no
// length cap.
template <std::size_t N>
struct fixed_string {
  char data[N]{};

  constexpr fixed_string() noexcept = default;
  constexpr fixed_string(char const (&s)[N]) noexcept {
    for (std::size_t i = 0; i < N; ++i) data[i] = s[i];
  }

  static constexpr std::size_t size() noexcept { return N - 1; }
  constexpr std::string_view view() const noexcept { return {data, N - 1}; }
};

template <std::size_t N>
fixed_string(char const (&)[N]) -> fixed_string<N>;

namespace detail {

template <fixed_string S, std::size_t Begin, std::size_t Size>
constexpr fixed_string<Size + 1> substring() noexcept {
  fixed_string<Size + 1> result;
  for (std::size_t i = 0; i < Size; ++i) result.data[i] = S.data[Begin + i];
  return result;
}

}  // namespace detail
}  // namespace eaux::text::static_format
