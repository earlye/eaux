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

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace eaux::text::static_format {

namespace detail {

// The lowest argument index below `given` that no segment uses, or `given` when all are used.
template <class Segments>
constexpr std::size_t first_unused_argument(Segments const& segments, std::size_t given) noexcept {
  for (std::size_t arg = 0; arg < given; ++arg) {
    bool used = false;
    for (auto const& seg : segments) used = used || (seg.kind == segment_kind::argument && seg.arg == arg);
    if (!used) return arg;
  }
  return given;
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
    render(sink, args...);
    return std::move(sink).take();
  }

  // Writes to `out` and returns the iterator past the output.
  template <std::output_iterator<char const&> Out, class... Args>
  EAUX_STATIC_FORMAT_ALWAYS_INLINE Out to(Out out, Args const&... args) const {
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

  // Writes the longest prefix of the output that fits in `n` bytes and ends on a grapheme
  // cluster boundary, and returns the iterator past it with the size the whole output would have
  // had. The output is formatted in full first: a cluster can span a literal and an argument, so
  // only the complete text shows where the boundaries are.
  template <std::output_iterator<char const&> Out, class... Args>
  std::format_to_n_result<Out> to_n(Out out, std::iter_difference_t<Out> n, Args const&... args) const {
    detail::string_sink sink;
    render(sink, args...);
    std::string_view const all = sink.view();
    std::string_view const fits = all.substr(0, unicode::byte_prefix(all, n > 0 ? static_cast<std::size_t>(n) : 0));
    if constexpr (detail::char_contiguous<Out>) {
      if (!fits.empty()) std::memcpy(std::to_address(out), fits.data(), fits.size());
      return {out + static_cast<std::iter_difference_t<Out>>(fits.size()), static_cast<std::iter_difference_t<Out>>(all.size())};
    } else {
      return {std::copy(fits.begin(), fits.end(), std::move(out)), static_cast<std::iter_difference_t<Out>>(all.size())};
    }
  }

 private:
  template <class Sink, class... Args>
  EAUX_STATIC_FORMAT_ALWAYS_INLINE static void render(Sink& sink, Args const&... args) {
    constexpr std::size_t unused = detail::first_unused_argument(parsed.segments, sizeof...(Args));
    constexpr bool all_used = detail::every_argument_used<Syn, unused, sizeof...(Args)>();
    if constexpr (detail::argument_count<Syn, arguments, sizeof...(Args)>() && all_used) {
      detail::argument_pack<std::index_sequence_for<Args...>, Args...> const pack{{args}...};
      [&]<std::size_t... I>(std::index_sequence<I...>) EAUX_STATIC_FORMAT_ALWAYS_INLINE_LAMBDA {
        (emit<I>(sink, pack), ...);
      }(std::make_index_sequence<info.segments>{});
    }
  }

  template <std::size_t I, class Sink, class Pack>
  static void emit(Sink& sink, [[maybe_unused]] Pack const& pack) {
    constexpr detail::segment seg = parsed.segments[I];
    if constexpr (seg.kind == detail::segment_kind::literal) {
      sink.put(std::string_view(parsed.text + seg.begin, seg.size));
    } else {
      detail::write_argument<detail::without_offsets(seg.spec), seg.arg,
                             detail::substring<S, seg.spec.raw_begin, seg.spec.raw_size>()>(
          sink, detail::pick<seg.arg>(pack));
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
  template <class Out, class... Args>
  Out to(Out out, Args const&...) const {
    return out;
  }
  template <class Out, class... Args>
  std::format_to_n_result<Out> to_n(Out out, std::iter_difference_t<Out>, Args const&...) const {
    return {out, 0};
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
