#pragma once

#include <eaux/text/unicode/detail/tables.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <ranges>
#include <string_view>

namespace eaux::text::unicode {

inline constexpr std::string_view version = detail::unicode_version;

enum class grapheme_break : std::uint8_t {
  other,
  cr,
  lf,
  control,
  extend,
  zwj,
  regional_indicator,
  prepend,
  spacing_mark,
  l,
  v,
  t,
  lv,
  lvt,
};

enum class indic_conjunct_break : std::uint8_t { none, linker, consonant, extend };

namespace detail {

constexpr std::uint8_t properties(char32_t cp) noexcept {
  if (cp >= 0x110000) return 0;
  auto const block = property_index[cp >> property_shift];
  return property_blocks[(std::size_t{block} << property_shift) | (cp & ((1u << property_shift) - 1))];
}

}  // namespace detail

constexpr grapheme_break grapheme_break_property(char32_t cp) noexcept {
  return static_cast<grapheme_break>(detail::properties(cp) & 0x0f);
}

constexpr bool is_extended_pictographic(char32_t cp) noexcept { return (detail::properties(cp) & 0x10) != 0; }

constexpr indic_conjunct_break indic_conjunct_break_property(char32_t cp) noexcept {
  return static_cast<indic_conjunct_break>((detail::properties(cp) >> 5) & 0x03);
}

// The estimated width of a code point in [format.string.std]: 2 for East_Asian_Width W or F
// and three pictograph ranges, 1 for everything else.
constexpr std::size_t code_point_width(char32_t cp) noexcept { return (detail::properties(cp) & 0x80) ? 2 : 1; }

struct decoded {
  char32_t code_point;
  std::size_t length;
  bool valid;
};

// Decodes the code point starting at byte `pos`. An ill-formed sequence decodes as U+FFFD
// covering one byte, so callers make progress and malformed input passes through unchanged.
constexpr decoded decode(std::string_view s, std::size_t pos) noexcept {
  auto const byte = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
  constexpr decoded invalid{U'\uFFFD', 1, false};
  unsigned const b0 = byte(pos);
  if (b0 < 0x80) return {b0, 1, true};

  std::size_t length;
  char32_t cp;
  char32_t min;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    length = 2, cp = b0 & 0x1F, min = 0x80;
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    length = 3, cp = b0 & 0x0F, min = 0x800;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    length = 4, cp = b0 & 0x07, min = 0x10000;
  } else {
    return invalid;
  }
  if (s.size() - pos < length) return invalid;
  for (std::size_t i = 1; i < length; ++i) {
    unsigned const b = byte(pos + i);
    if ((b & 0xC0) != 0x80) return invalid;
    cp = (cp << 6) | (b & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return invalid;
  return {cp, length, true};
}

// Offset of the first byte >= 0x80, or s.size() when the string is ASCII. At run time this ORs
// whole words together so the compiler can vectorize it; only the final partial word is scanned
// bytewise.
constexpr std::size_t ascii_prefix(std::string_view s) noexcept {
  std::size_t i = 0;
  if !consteval {
    constexpr std::uint64_t high = 0x8080808080808080u;
    auto const word = [&](std::size_t at) {
      std::uint64_t w;
      std::memcpy(&w, s.data() + at, sizeof w);
      return w;
    };
    for (; s.size() - i >= 32; i += 32) {
      if ((word(i) | word(i + 8) | word(i + 16) | word(i + 24)) & high) break;
    }
    for (; s.size() - i >= 8; i += 8) {
      if (word(i) & high) break;
    }
  }
  for (; i < s.size(); ++i) {
    if (static_cast<unsigned char>(s[i]) >= 0x80) return i;
  }
  return s.size();
}

constexpr bool is_ascii(std::string_view s) noexcept { return ascii_prefix(s) == s.size(); }

// End of the extended grapheme cluster (UAX #29) that starts at byte `pos`.
constexpr std::size_t next_grapheme_break(std::string_view s, std::size_t pos) noexcept {
  if (pos >= s.size()) return s.size();
  using enum grapheme_break;

  auto const first = decode(s, pos);
  auto prev = grapheme_break_property(first.code_point);
  std::size_t i = pos + first.length;

  // GB11: Extended_Pictographic Extend* ZWJ x Extended_Pictographic.
  enum class emoji { none, pictographic, zwj } emoji_state =
      is_extended_pictographic(first.code_point) ? emoji::pictographic : emoji::none;
  // GB12/GB13: count of consecutive regional indicators ending at prev.
  std::size_t regional_indicators = prev == regional_indicator ? 1 : 0;
  // GB9c: InCB=Linker InCB=Extend* x InCB=Consonant.
  bool linked = indic_conjunct_break_property(first.code_point) == indic_conjunct_break::linker;

  while (i < s.size()) {
    auto const next = decode(s, i);
    auto const cur = grapheme_break_property(next.code_point);
    auto const incb = indic_conjunct_break_property(next.code_point);
    bool const pictographic = is_extended_pictographic(next.code_point);

    bool joined;
    if (prev == cr && cur == lf) {
      joined = true;  // GB3
    } else if (prev == control || prev == cr || prev == lf || cur == control || cur == cr || cur == lf) {
      joined = false;  // GB4, GB5
    } else if (prev == l && (cur == l || cur == v || cur == lv || cur == lvt)) {
      joined = true;  // GB6
    } else if ((prev == lv || prev == v) && (cur == v || cur == t)) {
      joined = true;  // GB7
    } else if ((prev == lvt || prev == t) && cur == t) {
      joined = true;  // GB8
    } else if (cur == extend || cur == zwj || cur == spacing_mark || prev == prepend) {
      joined = true;  // GB9, GB9a, GB9b
    } else if (linked && incb == indic_conjunct_break::consonant) {
      joined = true;  // GB9c
    } else if (emoji_state == emoji::zwj && pictographic) {
      joined = true;  // GB11
    } else if (prev == regional_indicator && cur == regional_indicator) {
      joined = regional_indicators % 2 == 1;  // GB12, GB13
    } else {
      joined = false;  // GB999
    }
    if (!joined) break;

    if (pictographic) {
      emoji_state = emoji::pictographic;
    } else if (emoji_state == emoji::pictographic && cur == extend) {
    } else if (emoji_state == emoji::pictographic && cur == zwj) {
      emoji_state = emoji::zwj;
    } else {
      emoji_state = emoji::none;
    }
    regional_indicators = cur == regional_indicator ? regional_indicators + 1 : 0;
    if (incb == indic_conjunct_break::linker) {
      linked = true;
    } else if (incb != indic_conjunct_break::extend) {
      linked = false;
    }

    prev = cur;
    i += next.length;
  }
  return i;
}

// Width of one grapheme cluster: the width of its first code point.
constexpr std::size_t grapheme_width(std::string_view grapheme) noexcept {
  return grapheme.empty() ? 0 : code_point_width(decode(grapheme, 0).code_point);
}

// Estimated display width per [format.string.std]: the sum over extended grapheme clusters of
// the width of each cluster's first code point. ASCII text has width equal to its length.
constexpr std::size_t display_width(std::string_view s) noexcept {
  std::size_t const ascii = ascii_prefix(s);
  if (ascii == s.size()) return s.size();
  // The cluster holding the last ASCII byte may continue past it (e.g. "e" + U+0301).
  std::size_t pos = ascii == 0 ? 0 : ascii - 1;
  std::size_t width = pos;
  while (pos < s.size()) {
    std::size_t const end = next_grapheme_break(s, pos);
    width += grapheme_width(s.substr(pos, end - pos));
    pos = end;
  }
  return width;
}

struct prefix {
  std::size_t size;   // bytes
  std::size_t width;  // display width
};

// The longest prefix made of whole grapheme clusters whose display width is at most `max_width`.
constexpr prefix width_prefix(std::string_view s, std::size_t max_width) noexcept {
  std::size_t const ascii = ascii_prefix(s);
  if (ascii == s.size() || ascii > max_width) {
    std::size_t n = max_width < s.size() ? max_width : s.size();
    if (n > 0 && n < s.size() && s[n - 1] == '\r' && s[n] == '\n') --n;  // CR LF is one cluster
    return {n, n};
  }
  std::size_t pos = ascii == 0 ? 0 : ascii - 1;
  std::size_t width = pos;
  while (pos < s.size()) {
    std::size_t const end = next_grapheme_break(s, pos);
    std::size_t const w = grapheme_width(s.substr(pos, end - pos));
    if (width + w > max_width) break;
    width += w;
    pos = end;
  }
  return {pos, width};
}

// The longest prefix made of whole grapheme clusters that fits in `max_bytes`.
constexpr std::size_t byte_prefix(std::string_view s, std::size_t max_bytes) noexcept {
  if (max_bytes >= s.size()) return s.size();
  std::size_t const ascii = ascii_prefix(s);
  if (ascii > max_bytes) {
    // Every boundary between two ASCII bytes is a cluster boundary except inside CR LF.
    return max_bytes > 0 && s[max_bytes - 1] == '\r' && s[max_bytes] == '\n' ? max_bytes - 1 : max_bytes;
  }
  std::size_t pos = ascii == 0 ? 0 : ascii - 1;
  while (pos < s.size()) {
    std::size_t const end = next_grapheme_break(s, pos);
    if (end > max_bytes) break;
    pos = end;
  }
  return pos;
}

// A view over the extended grapheme clusters of a string, each as a string_view. It only refers
// to the text, so it is a borrowed range: iterators outlive the view object.
class graphemes : public std::ranges::view_interface<graphemes> {
 public:
  class iterator {
   public:
    // A forward iterator in C++20 terms; its operator* returns by value, which the older
    // iterator categories count only as input.
    using iterator_concept = std::forward_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = std::string_view;

    constexpr iterator() = default;
    constexpr std::string_view operator*() const noexcept { return text_.substr(pos_, end_ - pos_); }
    constexpr iterator& operator++() noexcept {
      pos_ = end_;
      end_ = next_grapheme_break(text_, pos_);
      return *this;
    }
    constexpr iterator operator++(int) noexcept {
      auto copy = *this;
      ++*this;
      return copy;
    }
    friend constexpr bool operator==(iterator const& a, iterator const& b) noexcept { return a.pos_ == b.pos_; }

   private:
    friend class graphemes;
    constexpr iterator(std::string_view text, std::size_t pos) noexcept
        : text_(text), pos_(pos), end_(next_grapheme_break(text, pos)) {}

    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t end_ = 0;
  };

  constexpr graphemes() noexcept = default;
  constexpr explicit graphemes(std::string_view text) noexcept : text_(text) {}
  constexpr iterator begin() const noexcept { return {text_, 0}; }
  constexpr iterator end() const noexcept { return {text_, text_.size()}; }

 private:
  std::string_view text_;
};

}  // namespace eaux::text::unicode

template <>
inline constexpr bool std::ranges::enable_borrowed_range<eaux::text::unicode::graphemes> = true;
