#pragma once

#include <compare>
#include <cstddef>
#include <expected>
#include <format>
#include <string_view>

namespace eaux::text {

namespace detail {
struct ascii_literal;
}

// A string_view proven, once at construction, to hold no byte >= 0x80. Code that needs ASCII
// (fixed-width columns, protocol tokens, fast paths that count bytes as characters) can take
// this type instead of re-checking.
class ascii_string_view {
 public:
  using value_type = char;
  using size_type = std::size_t;
  using const_iterator = std::string_view::const_iterator;
  using iterator = const_iterator;

  constexpr ascii_string_view() noexcept = default;

  // The offset of the first non-ASCII byte when `s` is not ASCII.
  static constexpr std::expected<ascii_string_view, std::size_t> from(std::string_view s) noexcept {
    for (std::size_t i = 0; i < s.size(); ++i) {
      if (static_cast<unsigned char>(s[i]) >= 0x80) return std::unexpected(i);
    }
    return ascii_string_view(s);
  }

  constexpr std::string_view view() const noexcept { return view_; }
  constexpr operator std::string_view() const noexcept { return view_; }

  constexpr char const* data() const noexcept { return view_.data(); }
  constexpr std::size_t size() const noexcept { return view_.size(); }
  constexpr bool empty() const noexcept { return view_.empty(); }
  constexpr const_iterator begin() const noexcept { return view_.begin(); }
  constexpr const_iterator end() const noexcept { return view_.end(); }
  constexpr char operator[](std::size_t i) const noexcept { return view_[i]; }

  // Any substring of ASCII is ASCII, so slicing keeps the guarantee.
  constexpr ascii_string_view substr(std::size_t pos, std::size_t count = std::string_view::npos) const {
    return ascii_string_view(view_.substr(pos, count));
  }

  friend constexpr bool operator==(ascii_string_view a, ascii_string_view b) noexcept { return a.view_ == b.view_; }
  friend constexpr std::strong_ordering operator<=>(ascii_string_view a, ascii_string_view b) noexcept {
    return a.view_ <=> b.view_;
  }

 private:
  friend struct detail::ascii_literal;
  constexpr explicit ascii_string_view(std::string_view s) noexcept : view_(s) {}

  std::string_view view_;
};

namespace detail {
// Deliberately not constexpr: reaching it during constant evaluation is what makes a non-ASCII
// `_ascii` literal fail to compile, and its name is what the compiler reports.
inline void ascii_literal_contains_a_byte_ge_0x80() {}

struct ascii_literal {
  static consteval ascii_string_view make(char const* s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      if (static_cast<unsigned char>(s[i]) >= 0x80) detail::ascii_literal_contains_a_byte_ge_0x80();
    }
    return ascii_string_view(std::string_view(s, n));
  }
};
}  // namespace detail

inline namespace literals {
consteval ascii_string_view operator""_ascii(char const* s, std::size_t n) { return detail::ascii_literal::make(s, n); }
}  // namespace literals

}  // namespace eaux::text

template <>
struct std::formatter<eaux::text::ascii_string_view, char> : std::formatter<std::string_view, char> {
  template <class FormatContext>
  auto format(eaux::text::ascii_string_view s, FormatContext& ctx) const {
    return std::formatter<std::string_view, char>::format(s.view(), ctx);
  }
};
