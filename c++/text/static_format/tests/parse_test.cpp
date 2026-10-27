// Parse-level checks run entirely at compile time; this file passes by compiling.
#include <eaux/text/static_format/static_format.hpp>

#include <doctest/doctest.h>

#include <string_view>

namespace {

using namespace eaux::text::static_format::detail;
using namespace std::string_view_literals;

template <syntax Syn, std::size_t N>
constexpr auto parse_all(char const (&s)[N]) {
  struct result {
    parse_result info;
    std::array<segment, 16> segments;
    char text[64];
  } r{};
  std::string_view const view(s, N - 1);
  r.info = scan<Syn>(view);
  building_visitor<64, 16> builder;
  parse<Syn>(view, builder);
  r.segments = builder.out.segments;
  for (std::size_t i = 0; i < 64; ++i) r.text[i] = builder.out.text[i];
  return r;
}

template <std::size_t N>
constexpr field_spec printf_spec(char const (&s)[N]) {
  return parse_all<syntax::printf>(s).segments[0].spec;
}

template <std::size_t N>
constexpr bool literal_is(char const (&s)[N], std::size_t i, std::string_view expected) {
  auto const r = parse_all<syntax::printf>(s);
  return std::string_view(r.text + r.segments[i].begin, r.segments[i].size) == expected;
}

template <syntax Syn, std::size_t N>
constexpr parse_error error_of(char const (&s)[N]) {
  return scan<Syn>(std::string_view(s, N - 1)).error;
}

// Segments, as the original's segment_at tests checked them.
static_assert(parse_all<syntax::printf>("test %s foo").info.segments == 3);
static_assert(parse_all<syntax::printf>("test %s foo").segments[0].kind == segment_kind::literal);
static_assert(parse_all<syntax::printf>("test %s foo").segments[1].kind == segment_kind::argument);
static_assert(parse_all<syntax::printf>("test %s foo").segments[2].kind == segment_kind::literal);
static_assert(literal_is("test %3s foo", 0, "test "));
static_assert(literal_is("test %3s foo", 2, " foo"));
static_assert(parse_all<syntax::printf>("test %3s foo").segments[1].spec.width == 3);
static_assert(parse_all<syntax::printf>("").info.segments == 0);

// Escapes merge into the surrounding literal.
static_assert(parse_all<syntax::printf>("100%% done").info.segments == 1);
static_assert(literal_is("100%% done", 0, "100% done"));
static_assert(parse_all<syntax::brace>("{{x}}").info.segments == 1);

// Argument indexing.
static_assert(parse_all<syntax::printf>("%d %d %d").info.arguments == 3);
static_assert(parse_all<syntax::printf>("%2$s %1$s").segments[0].arg == 1);
static_assert(parse_all<syntax::printf>("%2$s %1$s").info.arguments == 2);
static_assert(parse_all<syntax::printf>("%1% %1%").info.arguments == 1);
static_assert(parse_all<syntax::printf>("%3%").info.arguments == 3);
static_assert(parse_all<syntax::printf>("%3%").segments[0].spec.syn == syntax::brace);
static_assert(parse_all<syntax::brace>("{1} {0} {1}").info.arguments == 2);
static_assert(parse_all<syntax::brace>("{} {}").segments[2].arg == 1);

// Flags, as the original's find_flags tests checked them, with alignment's new meanings.
static_assert(printf_spec("%1$f").alignment == align::none);
static_assert(printf_spec("%1$-f").alignment == align::left);
static_assert(printf_spec("%1$=f").alignment == align::internal);
static_assert(printf_spec("%1$^f").alignment == align::center);
static_assert(printf_spec("%1$>f").alignment == align::right);
static_assert(printf_spec("%1$+f").sign_mode == sign::plus);
static_assert(printf_spec("%1$#f").alternate);
static_assert(printf_spec("%1$0f").zero);
static_assert(printf_spec("%1$ f").sign_mode == sign::space);
static_assert(printf_spec("%-f").alignment == align::left);
static_assert(printf_spec("%+ f").sign_mode == sign::plus);
static_assert(printf_spec("% +f").sign_mode == sign::plus);
static_assert(printf_spec("%+#f").alternate && printf_spec("%+#f").sign_mode == sign::plus);
static_assert(printf_spec("%+#.4f").precision == 4);
static_assert(printf_spec("%--5d").alignment == align::left);

// Width and precision, as the original's find_width and find_precision tests checked them.
static_assert(printf_spec("%1$-2d").width == 2);
static_assert(printf_spec("%2d").width == 2);
static_assert(printf_spec("%d").width == 0);
static_assert(printf_spec("%#2d").width == 2);
static_assert(printf_spec("%#32d").width == 32);
static_assert(printf_spec("%1$-2.4d").precision == 4);
static_assert(printf_spec("%2.4d").precision == 4);
static_assert(printf_spec("%d").precision == no_precision);
static_assert(printf_spec("%.4d").precision == 4);
static_assert(printf_spec("%#.32d").precision == 32);
static_assert(printf_spec("%.f").precision == 0);
static_assert(printf_spec("%08x").zero && printf_spec("%08x").width == 8);

// Length modifiers are accepted and ignored.
static_assert(printf_spec("%lld").conversion == 'd');
static_assert(printf_spec("%hhx").conversion == 'x');
static_assert(printf_spec("%zu").conversion == 'u');
static_assert(printf_spec("%Lf").conversion == 'f');
static_assert(printf_spec("%-8.3lf").width == 8);

// Braced specs.
static_assert(printf_spec("%{ascii}s").ascii_only);
static_assert(printf_spec("%-12{ascii}s").width == 12);
static_assert(printf_spec("%{}s").conversion == 's');
static_assert(printf_spec("%-12{.3;kN}V").raw_size == 5);
static_assert(printf_spec("%-12{.3;kN}V").raw_begin == 5);

// The brace side reads the standard spec.
constexpr field_spec brace_spec(std::string_view s) {
  field_spec spec{};
  spec.std_error = parse_std_spec(s, spec);
  return spec;
}
static_assert(brace_spec("*^+#010.3e").fill.bytes[0] == '*');
static_assert(brace_spec("*^+#010.3e").alignment == align::center);
static_assert(brace_spec("*^+#010.3e").sign_mode == sign::plus);
static_assert(brace_spec("*^+#010.3e").alternate);
static_assert(brace_spec("*^+#010.3e").zero);
static_assert(brace_spec("*^+#010.3e").width == 10);
static_assert(brace_spec("*^+#010.3e").precision == 3);
static_assert(brace_spec("*^+#010.3e").conversion == 'e');
static_assert(brace_spec("\u00B7>8").fill.size == 2);
static_assert(brace_spec("<").alignment == align::left);
static_assert(brace_spec("").std_error == parse_error::none);
static_assert(brace_spec("Q").std_error == parse_error::std_spec_invalid);
static_assert(brace_spec("8.").std_error == parse_error::std_spec_invalid);
static_assert(brace_spec("Ld").std_error == parse_error::std_spec_locale);
static_assert(brace_spec("?").std_error == parse_error::std_spec_debug);
static_assert(brace_spec("dd").std_error == parse_error::std_spec_invalid);

// Every error the parsers report. tests/fail checks the messages they produce.
static_assert(error_of<syntax::printf>("abc %") == parse_error::printf_incomplete);
static_assert(error_of<syntax::printf>("%-8.3") == parse_error::printf_incomplete);
static_assert(error_of<syntax::printf>("%k") == parse_error::printf_unknown_conversion);
static_assert(error_of<syntax::printf>("%S") == parse_error::printf_wide_conversion);
static_assert(error_of<syntax::printf>("%C") == parse_error::printf_wide_conversion);
static_assert(error_of<syntax::printf>("%n") == parse_error::printf_n_conversion);
static_assert(error_of<syntax::printf>("%*d") == parse_error::printf_dynamic_width);
static_assert(error_of<syntax::printf>("%.*f") == parse_error::printf_dynamic_width);
static_assert(error_of<syntax::printf>("%0$d") == parse_error::printf_index_zero);
static_assert(error_of<syntax::printf>("%0%") == parse_error::printf_index_zero);
static_assert(error_of<syntax::printf>("%1$d %d") == parse_error::printf_mixed_indexing);
static_assert(error_of<syntax::printf>("%d %1%") == parse_error::printf_mixed_indexing);
static_assert(error_of<syntax::printf>("%{ascii s") == parse_error::printf_unterminated_braces);
static_assert(error_of<syntax::printf>("%{utf8}s") == parse_error::printf_unknown_key);
static_assert(error_of<syntax::printf>("%{ascii}d") == parse_error::printf_ascii_needs_s);
static_assert(error_of<syntax::printf>("%+v") == parse_error::printf_user_type_flags);
static_assert(error_of<syntax::printf>("%08V") == parse_error::printf_user_type_flags);
static_assert(error_of<syntax::printf>("%=8v") == parse_error::printf_user_type_flags);
static_assert(error_of<syntax::printf>("%-^5d") == parse_error::printf_conflicting_alignment);
static_assert(error_of<syntax::printf>("%-12^8v") == parse_error::printf_unknown_conversion);
static_assert(error_of<syntax::brace>("a}") == parse_error::brace_unmatched_close);
static_assert(error_of<syntax::brace>("{0") == parse_error::brace_unterminated);
static_assert(error_of<syntax::brace>("{:x") == parse_error::brace_unterminated);
static_assert(error_of<syntax::brace>("{} {0}") == parse_error::brace_mixed_indexing);
static_assert(error_of<syntax::brace>("{x}") == parse_error::brace_invalid_arg_id);
static_assert(error_of<syntax::brace>("{01}") == parse_error::brace_invalid_arg_id);
static_assert(error_of<syntax::brace>("{:{}}") == parse_error::brace_nested_field);

// Valid strings with the same characters.
static_assert(error_of<syntax::printf>("%-12v %^8V %>4s") == parse_error::none);
static_assert(error_of<syntax::brace>("{{}} {0:}") == parse_error::none);

// The compiled format exposes how many arguments it takes.
using namespace eaux::text::static_format::literals;
static_assert(decltype("x=%d y=%d"_pfmt)::arguments == 2);
static_assert(decltype("{2}"_bfmt)::arguments == 3);
static_assert(decltype("abc"_pfmt)::source() == "abc");

}  // namespace

TEST_CASE("parse-level checks compile") { CHECK(true); }
