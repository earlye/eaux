#pragma once

// Compile-time-parsed formatting:
//
//   using namespace eaux::text::static_format::literals;
//   std::string s = "0x%02X"_pfmt(255);          // printf syntax, plus boost's %N%
//   std::string t = "{:>8.3f}|{}"_bfmt(x, name);  // std::format syntax
//   char* end = "%d-%02d"_pfmt.to(buffer, y, m);
//   auto [out, size] = "%s"_pfmt.to_n(buffer, n, label);
//
// The format string is a template argument, parsed once by the compiler into an array of
// segments; each call expands that array over the arguments with a fold, so no parsing happens
// at run time and neither long strings nor many arguments recurse.

#include <eaux/text/static_format/detail/argument.hpp>
#include <eaux/text/static_format/detail/diagnostics.hpp>
#include <eaux/text/static_format/detail/fixed_string.hpp>
#include <eaux/text/static_format/detail/parse.hpp>
#include <eaux/text/static_format/detail/segment.hpp>
#include <eaux/text/static_format/detail/sink.hpp>
#include <eaux/text/static_format/detail/types.hpp>
#include <eaux/text/unicode/unicode.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace eaux::text::static_format {

namespace detail {

constexpr std::size_t grapheme_count(std::string_view s) noexcept {
  std::size_t n = 0;
  for (std::size_t pos = 0; pos < s.size(); pos = unicode::next_grapheme_break(s, pos)) ++n;
  return n;
}

template <std::size_t N>
constexpr std::array<std::size_t, N> grapheme_ends(std::string_view s) noexcept {
  std::array<std::size_t, N> ends{};
  std::size_t pos = 0;
  for (auto& end : ends) end = pos = unicode::next_grapheme_break(s, pos);
  return ends;
}

}  // namespace detail

template <detail::syntax Syn, fixed_string S>
class compiled_format {
  static constexpr auto info = detail::scan<Syn>(S.view());
  static constexpr auto parsed = detail::build<Syn, info.text_size, info.segments>(S.view());

 public:
  // The number of arguments the format string uses.
  static constexpr std::size_t arguments = info.arguments;

  static constexpr std::string_view source() noexcept { return S.view(); }

  template <class... Args>
  std::string operator()(Args const&... args) const {
    detail::string_sink sink;
    sink.out.reserve(info.text_size + 16 * sizeof...(Args));
    render(sink, args...);
    return std::move(sink.out);
  }

  // Writes to `out` and returns the iterator past the output.
  template <std::output_iterator<char const&> Out, class... Args>
  Out to(Out out, Args const&... args) const {
    if constexpr (detail::char_contiguous<Out>) {
      auto* const first = std::to_address(out);
      detail::pointer_sink sink{first};
      render(sink, args...);
      return out + (sink.out - first);
    } else {
      detail::iterator_sink<Out> sink{std::move(out)};
      render(sink, args...);
      return std::move(sink.out);
    }
  }

  // Writes at most `n` bytes, never splitting a grapheme cluster, and returns the iterator past
  // the output with the size the whole output would have had.
  template <std::output_iterator<char const&> Out, class... Args>
  std::format_to_n_result<Out> to_n(Out out, std::iter_difference_t<Out> n, Args const&... args) const {
    auto const room = n > 0 ? static_cast<std::size_t>(n) : std::size_t{0};
    if constexpr (detail::char_contiguous<Out>) {
      auto* const first = std::to_address(out);
      detail::truncating_sink<detail::pointer_sink> sink{{first}, room};
      render(sink, args...);
      return {out + (sink.inner.out - first), static_cast<std::iter_difference_t<Out>>(sink.total)};
    } else {
      detail::truncating_sink<detail::iterator_sink<Out>> sink{{std::move(out)}, room};
      render(sink, args...);
      return {std::move(sink.inner.out), static_cast<std::iter_difference_t<Out>>(sink.total)};
    }
  }

 private:
  static constexpr std::string_view literal(std::size_t i) noexcept {
    return {parsed.text + parsed.segments[i].begin, parsed.segments[i].size};
  }

  // Where each grapheme cluster of literal segment I ends, so to_n can cut it with a lookup.
  template <std::size_t I>
  static constexpr auto literal_ends = detail::grapheme_ends<detail::grapheme_count(literal(I))>(literal(I));

  template <class Sink, class... Args>
  static void render(Sink& sink, Args const&... args) {
    if constexpr (detail::argument_count<Syn, arguments, sizeof...(Args)>()) {
      [[maybe_unused]] void const* const pointers[] = {static_cast<void const*>(std::addressof(args))..., nullptr};
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        (emit<I, Args...>(sink, pointers), ...);
      }(std::make_index_sequence<info.segments>{});
    }
  }

  template <std::size_t I, class... Args, class Sink>
  static void emit(Sink& sink, [[maybe_unused]] void const* const* pointers) {
    constexpr detail::segment seg = parsed.segments[I];
    if constexpr (seg.kind == detail::segment_kind::literal) {
      constexpr std::string_view text(parsed.text + seg.begin, seg.size);
      if constexpr (seg.ascii) {
        sink.put(text, detail::text::ascii);
      } else if constexpr (Sink::truncating) {
        sink.put(text, std::span<std::size_t const>(literal_ends<I>));
      } else {
        sink.put(text, detail::text::utf8);
      }
    } else {
      using T = detail::type_at<seg.arg, Args...>;
      detail::write_argument<seg.spec, seg.arg, detail::substring<S, seg.spec.raw_begin, seg.spec.raw_size>()>(
          sink, *static_cast<T const*>(pointers[seg.arg]));
    }
  }
};

namespace detail {

// Stands in for a rejected format string so the error is reported once, at the literal.
struct invalid_format {
  template <class... Args>
  std::string operator()(Args const&...) const {
    return {};
  }
};

template <syntax Syn, fixed_string S>
constexpr auto make_format() {
  constexpr auto info = scan<Syn>(S.view());
  if constexpr (format_string_error<info.error, info.offset>()) {
    return compiled_format<Syn, S>{};
  } else {
    return invalid_format{};
  }
}

}  // namespace detail

inline namespace literals {

template <fixed_string S>
constexpr auto operator""_pfmt() {
  return detail::make_format<detail::syntax::printf, S>();
}

template <fixed_string S>
constexpr auto operator""_bfmt() {
  return detail::make_format<detail::syntax::brace, S>();
}

}  // namespace literals

}  // namespace eaux::text::static_format
