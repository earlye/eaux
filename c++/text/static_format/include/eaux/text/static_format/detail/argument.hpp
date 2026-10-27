#pragma once

#include <eaux/text/static_format/detail/diagnostics.hpp>
#include <eaux/text/static_format/detail/fixed_string.hpp>
#include <eaux/text/static_format/detail/segment.hpp>
#include <eaux/text/static_format/detail/sink.hpp>
#include <eaux/text/static_format/detail/types.hpp>
#include <eaux/text/static_format/detail/write.hpp>

#include <cstdint>
#include <format>
#include <iterator>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace eaux::text::static_format::detail {

// "{:" spec "}", for handing a brace field's spec to std::formatter<T> with compile-time checking.
template <fixed_string Raw>
struct std_field {
  static constexpr auto storage = [] {
    fixed_string<Raw.size() + 4> f;
    f.data[0] = '{';
    f.data[1] = ':';
    for (std::size_t i = 0; i < Raw.size(); ++i) f.data[i + 2] = Raw.data[i];
    f.data[Raw.size() + 2] = '}';
    return f;
  }();
  static constexpr std::string_view view = storage.view();
};

template <class T, class Out>
Out stream_to(Out out, T const& value) {
  std::ostringstream os;
  os.imbue(std::locale::classic());
  os << value;
  auto const text = std::move(os).str();
  return std::copy(text.begin(), text.end(), out);
}

// Output from a user formatter goes straight to the sink unless width or precision means it
// must be measured first.
template <field_spec Spec, class Sink, class Produce>
void write_produced(Sink& sink, Produce&& produce) {
  if constexpr (Spec.width == 0 && Spec.precision == no_precision) {
    produce(sink_iterator<Sink>(sink));
  } else {
    std::string text;
    produce(std::back_inserter(text));
    write_text<Spec, false>(sink, text);
  }
}

template <fixed_string Raw, class T, class Out>
Out format_static(Out out, T const& value) {
  using F = formatter<std::remove_cv_t<T>>;
  constexpr auto spec = F::parse(Raw.view());
  return F::format(spec, std::move(out), value);
}

template <field_spec Spec, fixed_string Raw, class Sink, class T>
void write_printf(Sink& sink, T const& value) {
  constexpr char conversion = Spec.conversion;
  if constexpr (conversion == 'd' || conversion == 'i' || conversion == 'u' || conversion == 'o' || conversion == 'x' ||
                conversion == 'X') {
    if constexpr (boolean<T>) {
      write_integer<Spec>(sink, static_cast<unsigned>(value));
    } else {
      write_integer<Spec>(sink, value);
    }
  } else if constexpr (conversion == 'c') {
    write_char<Spec>(sink, value);
  } else if constexpr (conversion == 's') {
    write_text<Spec, std::is_same_v<std::remove_cv_t<T>, ascii_string_view>>(sink, as_string_view(value));
  } else if constexpr (conversion == 'p') {
    if constexpr (std::is_null_pointer_v<T>) {
      write_pointer<Spec>(sink, 0);
    } else {
      write_pointer<Spec>(sink, reinterpret_cast<std::uintptr_t>(value));
    }
  } else if constexpr (conversion == 'v') {
    write_produced<Spec>(sink, [&](auto out) {
      if constexpr (std_formattable<T>) {
        std::format_to(std::move(out), "{}", value);
      } else {
        stream_to(std::move(out), value);
      }
    });
  } else if constexpr (conversion == 'V') {
    write_produced<Spec>(sink, [&](auto out) { format_static<Raw>(std::move(out), value); });
  } else {
    write_float<Spec>(sink, value);
  }
}

template <field_spec Spec, class Sink, class T>
void write_brace_builtin(Sink& sink, T const& value) {
  constexpr char type = Spec.conversion;
  if constexpr (boolean<T>) {
    if constexpr (type == 0 || type == 's') {
      write_text<Spec, true>(sink, value ? "true" : "false");
    } else {
      write_integer<Spec>(sink, static_cast<unsigned>(value));
    }
  } else if constexpr (brace_char<T>) {
    if constexpr (type == 0 || type == 'c') {
      write_char<Spec>(sink, value);
    } else {
      write_integer<Spec>(sink, static_cast<unsigned char>(value));
    }
  } else if constexpr (brace_integer<T>) {
    if constexpr (type == 'c') {
      write_char<Spec>(sink, static_cast<char>(value));
    } else {
      write_integer<Spec>(sink, value);
    }
  } else if constexpr (std::floating_point<T>) {
    write_float<Spec>(sink, value);
  } else if constexpr (brace_pointer<T>) {
    if constexpr (std::is_null_pointer_v<T>) {
      write_pointer<Spec>(sink, 0);
    } else {
      write_pointer<Spec>(sink, reinterpret_cast<std::uintptr_t>(value));
    }
  } else {
    write_text<Spec, std::is_same_v<std::remove_cv_t<T>, ascii_string_view>>(sink, as_string_view(value));
  }
}

// A user type on the brace side owns its whole spec, as with std::format: static_format's
// formatter first, then std::formatter, then operator<< when the spec is empty.
template <fixed_string Raw, class Sink, class T>
void write_brace_user(Sink& sink, T const& value) {
  write_produced<field_spec{.syn = syntax::brace}>(sink, [&](auto out) {
    if constexpr (static_formattable<T>) {
      format_static<Raw>(std::move(out), value);
    } else if constexpr (std_formattable<T>) {
      std::format_to(std::move(out), std_field<Raw>::view, value);
    } else {
      stream_to(std::move(out), value);
    }
  });
}

template <field_spec Spec, std::size_t Arg, fixed_string Raw, class Sink, class T>
void write_argument(Sink& sink, T const& value) {
  if constexpr (Spec.syn == syntax::printf) {
    if constexpr (printf_argument<Arg + 1, Spec.conversion, Spec.ascii_only, T>()) write_printf<Spec, Raw>(sink, value);
  } else if constexpr (brace_builtin<T>) {
    constexpr bool arithmetic = Spec.sign_mode != sign::none || Spec.alternate || Spec.zero;
    if constexpr (brace_builtin_argument<Arg, Spec.conversion, T, Spec.std_error, arithmetic,
                                         Spec.precision != no_precision>()) {
      write_brace_builtin<Spec>(sink, value);
    }
  } else {
    if constexpr (brace_user_argument<Arg, T, (Raw.size() != 0)>()) write_brace_user<Raw>(sink, value);
  }
}

}  // namespace eaux::text::static_format::detail
