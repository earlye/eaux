# eaux::text::static_format

Formatting with the format string parsed by the compiler. Two literal suffixes, one per syntax:

- `"0x%02X"_pfmt` takes printf syntax, plus boost's `%N%`.
- `"{:#04x}"_bfmt` takes `std::format`'s replacement-field syntax.

The string becomes a template argument, is parsed at compile time into an array of segments,
and every call expands that array over the arguments. Nothing is parsed at run time, a bad
format string or a mismatched argument fails the build, and neither long strings nor dozens of
arguments approach template-depth limits. Header-only, C++23, CMake target
`eaux::text::static_format` (which links `eaux::text::ascii` and `eaux::text::unicode`).

```cpp
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

std::string s = "0x%02X"_pfmt(255);                         // "0xFF"
std::string t = "{:>8.3f}|{:<6}|"_bfmt(3.14159, "σ");  // "   3.142|σ     |"

char buffer[64];
char* end = "%d-%02d-%02d"_pfmt.to(buffer, 2026, 10, 26);   // any output iterator
auto [out, size] = "%-12s"_pfmt.to_n(buffer, 8, label);     // size: the untruncated length
```

## printf syntax

`%[N$][flags][width][.precision][length][{spec}]conversion`, `%%`, and `%N%`.

| Conversion | Accepts |
|---|---|
| `d i` | integral types except `bool`, `char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t` |
| `u o x X` | any integral type; negative values are reinterpreted as unsigned, as in C |
| `f F e E g G a A` | floating point |
| `c` | character types, including `signed char` and `unsigned char` (an `int` needs a cast) |
| `s` | `char const*`, `char` arrays, `std::string`, `std::string_view`, `eaux::text::ascii_string_view` |
| `p` | pointers |
| `v` | anything with a `std::formatter`, else anything with `operator<<` |
| `V` | types with an `eaux::text::static_format::formatter` (below) |

- Flags `- + # space 0` mean what they mean in C. `^` centers, `=` pads between the sign or
  base prefix and the digits, and `>` right-aligns explicitly.
- Length modifiers (`hh h l ll z j t L`) are accepted and ignored, so `sprintf` strings paste in.
- `%N$` and `%N%` select arguments by position (from 1) and may repeat; a string cannot mix them
  with sequential conversions. `%N%` formats like `{N-1}` would.
- The argument count must match exactly.
- `%v` and `%V` take width, precision and the alignment flags `- ^ >` only.
- A braced spec before the conversion carries extra keys. The only built-in key is `ascii`:
  `%{ascii}s` requires an `ascii_string_view` argument. For `%V` the braces hold the user
  formatter's spec, e.g. `%-12{.3;kN}V`.
- `%c` writes a `char16_t`, `char32_t` or `wchar_t` as UTF-8; a surrogate or a value above
  U+10FFFF becomes U+FFFD.
- `%C`, `%S`, `%n` and `*` widths are compile errors.

## Brace syntax

`{[arg-id][:[[fill]align][sign][#][0][width][.precision][type]]}` with `{{` and `}}`, following
`std::format`'s rules for the built-in types (including that extra arguments are allowed).
Not supported yet: nested `{}` for dynamic width or precision, the `L` option, the `?` type,
and chrono or range specs.

A field on any other type uses, in order: `eaux::text::static_format::formatter<T>`, then
`std::formatter<T>` (its spec checked at compile time), then `operator<<` if the spec is empty.

## User types

```cpp
template <> struct eaux::text::static_format::formatter<force> {
  struct spec { int decimals; bool kilo; };                  // a structural type
  static constexpr spec parse(std::string_view text);        // throw to reject: fails the build
  template <class Out> static Out format(spec const&, Out out, force const&);
};

"%-12{.3;kN}V"_pfmt(f);   // parse(".3;kN") runs at compile time
"{:.3;kN}"_bfmt(f);
```

## Text

Text is UTF-8 everywhere and the unit is the extended grapheme cluster: width counts display
columns (East Asian wide characters count 2), string precision is a column budget that never
splits a cluster, and `to_n` stops at the last whole cluster that fits. This departs from C's
byte counting on purpose, so columns stay aligned when a label is `µs` or `σ_max`. Runtime
strings are scanned for non-ASCII bytes a word at a time and take a length-equals-width fast
path when there are none; `ascii_string_view` skips even that scan. Malformed UTF-8 passes
through. Numbers go through `std::to_chars`, so output is always "C"-locale. MSVC builds need
`/utf-8`, which the CMake target adds. See `docs/adr/`.

## Benchmarks

`make bench` (from `c++/`) builds `bench/` optimized with each compiler and runs it. CI only
builds it. Each case writes into a preallocated buffer; the `string_` cases return
`std::string`. The `row` cases format a 30-argument table row (two `%-12s` labels and 28
`%10.4f`), and `row_ascii` passes the labels as `ascii_string_view`.

Median ns per call (5 repetitions, pinned to one core) on an AMD Ryzen 9 3900X, Linux 7.0,
CMake Release (`-O3`), libstdc++ 15, fmt 12.2.0, Google Benchmark 1.10.0:

| case | snprintf | std::format_to | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|---:|
| `0x%02X`, g++ 15.2 | 47.2 | 62.0 | 9.6 | **8.7** | **8.7** |
| `0x%02X`, clang 21.1 | 46.7 | 54.9 | 12.4 | **6.0** | 6.1 |
| `%8.3f`, g++ | 202 | 166 | **40.8** | 60.7 | 61.5 |
| `%8.3f`, clang | 211 | 124 | **42.9** | 53.6 | 51.8 |
| short mixed line, g++ | 303 | 284 | **59.7** | 76.0 | 76.5 |
| short mixed line, clang | 303 | 230 | **62.8** | 66.2 | 66.6 |
| 30-argument row, g++ | 5829 | 4969 | 1957 | **1714** | 1942 |
| 30-argument row, clang | 6007 | 4127 | 2242 | **1527** | 1610 |
| row, `ascii_string_view` labels, g++ | | | | 1730 | 1925 |
| row, `ascii_string_view` labels, clang | | | | 1522 | 1599 |

| returning `std::string` | std::format | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|
| `0x%02X`, g++ | 72.0 | **16.5** | 30.3 | 30.6 |
| `0x%02X`, clang | 65.3 | **20.4** | 38.0 | 37.3 |
| short mixed line, g++ | 317 | 159 | **143** | **143** |
| short mixed line, clang | 257 | 166 | 169 | **155** |
| 30-argument row, g++ | 5019 | 2204 | **2129** | 2315 |
| 30-argument row, clang | 4157 | 2853 | **2050** | 2267 |

Where it loses to `FMT_COMPILE`:

- A lone floating-point conversion is 20-50% slower, and the short mixed line, which contains
  one, 5-28% slower. static_format converts with `std::to_chars(value, fixed, precision)`, and
  libstdc++'s implementation of that is slower than fmt's own algorithm.
- For a tiny result returned as `std::string` (`0x%02X`), static_format takes 30-38 ns against
  fmt's 16-20.

It is ahead on integers, on the 30-argument row, and on the mixed line returned as
`std::string` (except `_pfmt` under clang, 169 ns against 166). The `ascii_string_view` row is
no faster than the plain one, within noise: scanning two 12-byte labels for non-ASCII bytes
costs almost nothing.
