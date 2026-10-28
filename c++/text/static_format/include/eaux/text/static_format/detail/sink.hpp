#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

// Keeps a rarely taken path out of line so the common path inlines at every call site; without
// it g++ inlines string_sink's spill into each put and the std::string result costs ~60% more.
#if defined(_MSC_VER) && !defined(__clang__)
#define EAUX_STATIC_FORMAT_NOINLINE __declspec(noinline)
#else
#define EAUX_STATIC_FORMAT_NOINLINE [[gnu::noinline]]
#endif

// Forces the per-call glue (to, render, its segment lambda, write_float) inline: in a caller that
// formats many fields the renderer otherwise crosses clang's inline cost threshold and is called
// out of line. The float conversion itself stays out of line. MSVC has no lambda form.
#if defined(_MSC_VER) && !defined(__clang__)
#define EAUX_STATIC_FORMAT_ALWAYS_INLINE __forceinline
#define EAUX_STATIC_FORMAT_ALWAYS_INLINE_LAMBDA
#else
#define EAUX_STATIC_FORMAT_ALWAYS_INLINE [[gnu::always_inline]] inline
#define EAUX_STATIC_FORMAT_ALWAYS_INLINE_LAMBDA __attribute__((always_inline))
#endif

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

// Builds a std::string through a stack buffer, so a result that fits is constructed once at its
// final size (often within the small-string buffer) and only a longer one touches the heap. The
// fast path is a bounds check and a memcpy, small enough to inline at every call; spilling to the
// heap is out of line. A spilled sink marks its buffer full, so the fast path never runs again.
class string_sink {
 public:
  string_sink() = default;
  string_sink(string_sink const&) = delete;
  string_sink& operator=(string_sink const&) = delete;

  void put(std::string_view s) {
    if (s.size() <= sizeof buffer_ - size_) [[likely]] {
      if (!s.empty()) std::memcpy(buffer_ + size_, s.data(), s.size());
      size_ += s.size();
    } else {
      put_heap(s);
    }
  }
  void put(char c) { put(std::string_view(&c, 1)); }
  void fill(char c, std::size_t n) {
    if (n <= sizeof buffer_ - size_) [[likely]] {
      if (n) std::memset(buffer_ + size_, c, n);
      size_ += n;
    } else {
      fill_heap(c, n);
    }
  }

  std::string_view view() const noexcept { return spilled_ ? std::string_view(heap_) : std::string_view(buffer_, size_); }
  std::string take() && { return spilled_ ? std::move(heap_) : std::string(buffer_, size_); }

 private:
  EAUX_STATIC_FORMAT_NOINLINE void put_heap(std::string_view s) { spill().append(s); }
  EAUX_STATIC_FORMAT_NOINLINE void fill_heap(char c, std::size_t n) { spill().append(n, c); }

  std::string& spill() {
    if (!spilled_) {
      heap_.reserve(2 * sizeof buffer_);
      heap_.assign(buffer_, size_);
      spilled_ = true;
      size_ = sizeof buffer_;
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
