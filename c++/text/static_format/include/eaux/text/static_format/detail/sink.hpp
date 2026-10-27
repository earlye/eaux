#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

// Sinks receive the formatted output in pieces.
namespace eaux::text::static_format::detail {

struct pointer_sink {
  char* out;

  void put(std::string_view s) noexcept {
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
  Out out;

  void put(std::string_view s) { out = std::copy(s.begin(), s.end(), std::move(out)); }
  void put(char c) { *out++ = c; }
  void fill(char c, std::size_t n) { out = std::fill_n(std::move(out), n, c); }
};

// Builds a std::string through a stack buffer, so short results are constructed once at their
// final size (often within the small-string buffer) and long ones grow geometrically.
class string_sink {
 public:

  void put(std::string_view s) {
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

  std::string_view view() const noexcept { return spilled_ ? std::string_view(heap_) : std::string_view(buffer_, size_); }
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
