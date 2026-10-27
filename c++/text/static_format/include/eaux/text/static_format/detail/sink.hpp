#pragma once

#include <eaux/text/unicode/unicode.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

// Sinks receive the formatted output in pieces. A piece is tagged ASCII or UTF-8 so the
// truncating sink knows whether any cut is valid or it must find a grapheme boundary.
namespace eaux::text::static_format::detail {

enum class text : unsigned char { ascii, utf8 };

struct pointer_sink {
  static constexpr bool truncating = false;
  char* out;

  void put(std::string_view s, text = text::ascii) noexcept {
    if (!s.empty()) std::memcpy(out, s.data(), s.size());
    out += s.size();
  }
  void put(char c) noexcept { *out++ = c; }
  void fill(char c, std::size_t n) noexcept {
    if (n) std::memset(out, c, n);
    out += n;
  }
};

template <class Out>
struct iterator_sink {
  static constexpr bool truncating = false;
  Out out;

  void put(std::string_view s, text = text::ascii) { out = std::copy(s.begin(), s.end(), std::move(out)); }
  void put(char c) { *out++ = c; }
  void fill(char c, std::size_t n) { out = std::fill_n(std::move(out), n, c); }
};

// Builds a std::string through a stack buffer, so short results are constructed once at their
// final size (often within the small-string buffer) and long ones grow geometrically.
class string_sink {
 public:
  static constexpr bool truncating = false;

  void put(std::string_view s, text = text::ascii) {
    if (!spilled_ && s.size() <= sizeof buffer_ - size_) {
      if (!s.empty()) std::memcpy(buffer_ + size_, s.data(), s.size());
      size_ += s.size();
    } else {
      spill().append(s);
    }
  }
  void put(char c) { put(std::string_view(&c, 1)); }
  void fill(char c, std::size_t n) {
    if (!spilled_ && n <= sizeof buffer_ - size_) {
      if (n) std::memset(buffer_ + size_, c, n);
      size_ += n;
    } else {
      spill().append(n, c);
    }
  }

  std::string take() && { return spilled_ ? std::move(heap_) : std::string(buffer_, size_); }

 private:
  std::string& spill() {
    if (!spilled_) {
      heap_.reserve(2 * sizeof buffer_);
      heap_.assign(buffer_, size_);
      spilled_ = true;
    }
    return heap_;
  }

  char buffer_[512];
  std::size_t size_ = 0;
  bool spilled_ = false;
  std::string heap_;
};

// Counts every byte but writes only whole grapheme clusters while they fit; after the first
// piece that does not fit it writes nothing more, so later short pieces cannot follow a gap.
template <class Inner>
struct truncating_sink {
  static constexpr bool truncating = true;
  Inner inner;
  std::size_t room;
  std::size_t total = 0;
  bool full = false;

  void put(std::string_view s, text kind = text::ascii) {
    total += s.size();
    if (full) return;
    if (s.size() <= room) {
      inner.put(s, kind);
      room -= s.size();
      return;
    }
    inner.put(s.substr(0, kind == text::ascii ? room : unicode::byte_prefix(s, room)), kind);
    full = true;
  }
  // A literal whose cluster ends were computed at compile time.
  void put(std::string_view s, std::span<std::size_t const> ends) {
    total += s.size();
    if (full) return;
    if (s.size() <= room) {
      inner.put(s, text::utf8);
      room -= s.size();
      return;
    }
    auto const fits = std::upper_bound(ends.begin(), ends.end(), room);
    inner.put(s.substr(0, fits == ends.begin() ? 0 : *std::prev(fits)), text::utf8);
    full = true;
  }
  void put(char c) {
    ++total;
    if (full) return;
    if (room == 0) {
      full = true;
      return;
    }
    inner.put(c);
    --room;
  }
  void fill(char c, std::size_t n) {
    total += n;
    if (full) return;
    std::size_t const m = std::min(n, room);
    inner.fill(c, m);
    room -= m;
    full = m < n;
  }
};

// An output iterator over a sink, for formatters that write through iterators.
template <class Sink>
class sink_iterator {
 public:
  using iterator_category = std::output_iterator_tag;
  using value_type = void;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = void;

  sink_iterator() = default;
  explicit sink_iterator(Sink& sink) noexcept : sink_(&sink) {}
  // Assignment through a const iterator is what std::indirectly_writable asks of a proxy.
  sink_iterator const& operator*() const noexcept { return *this; }
  sink_iterator const& operator=(char c) const {
    sink_->put(c);
    return *this;
  }
  sink_iterator& operator++() noexcept { return *this; }
  sink_iterator operator++(int) noexcept { return *this; }

 private:
  Sink* sink_ = nullptr;
};

}  // namespace eaux::text::static_format::detail
