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
- Every argument passed must be used at least once, in both syntaxes; an unused argument, or a
  reference past the last one, is a compile error. This differs from `std::format`, which
  allows extra arguments (see ADR 0002).
- `%v` and `%V` take width, precision and the alignment flags `- ^ >` only.
- A braced spec before the conversion carries extra keys. The only built-in key is `ascii`:
  `%{ascii}s` requires an `ascii_string_view` argument. For `%V` the braces hold the user
  formatter's spec, e.g. `%-12{.3;kN}V`.
- `%c` writes a `char16_t`, `char32_t` or `wchar_t` as UTF-8; a surrogate or a value above
  U+10FFFF becomes U+FFFD.
- `%C`, `%S`, `%n` and `*` widths are compile errors.

## Brace syntax

`{[arg-id][:[[fill]align][sign][#][0][width][.precision][type]]}` with `{{` and `}}`, following
`std::format`'s rules for the built-in types.
Not supported yet: nested `{}` for dynamic width or precision, the `L` option and the `?` type.
Chrono and range specs (`{:%Y-%m-%d}`, `{::x}`) work wherever the standard library formats
those types, because such fields go to `std::formatter` (below). One deliberate difference from
`std::format`: `{:c}` on an integer writes that code point as UTF-8 (`{:c}` of `0x4E2D` is
`中`), and a value that is not a Unicode scalar value becomes U+FFFD instead of throwing.

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

Median ns per call on an AMD Ryzen 9 3900X, Linux 7.0, CMake Release (`-O3`), libstdc++ 15,
fmt 12.2.0, Google Benchmark 1.10.0, measured with
`make bench BENCH_PREFIX="taskset -c 5" BENCH_ARGS="--benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_min_time=0.3s"`:

| case | snprintf | std::format_to | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|---:|
| `0x%02X`, g++ 15.2 | 47.6 | 68.3 | 9.9 | 8.9 | **8.9** |
| `0x%02X`, clang 21.1 | 46.3 | 55.8 | 12.6 | 6.1 | **6.0** |
| `%8.3f`, g++ | 208 | 123 | **41.9** | 63.8 | 58.9 |
| `%8.3f`, clang | 211 | 125 | **43.1** | 52.6 | 52.5 |
| short mixed line, g++ | 314 | 265 | **59.0** | 80.9 | 89.3 |
| short mixed line, clang | 303 | 230 | **62.1** | 66.3 | 67.6 |
| 30-argument row, g++ | 6058 | 4014 | 2026 | 1818 | **1813** |
| 30-argument row, clang | 5889 | 4099 | 2329 | **1546** | 1580 |
| row, `ascii_string_view` labels, g++ | | | | 1804 | 1768 |
| row, `ascii_string_view` labels, clang | | | | 1500 | 1570 |

| returning `std::string` | std::format | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|
| `0x%02X`, g++ | 79.7 | **16.2** | 30.6 | 30.5 |
| `0x%02X`, clang | 64.1 | **19.3** | 34.8 | 35.1 |
| short mixed line, g++ | 291 | 161 | **151** | **151** |
| short mixed line, clang | 255 | 167 | 157 | **153** |
| 30-argument row, g++ | 4294 | 2346 | 2282 | **2109** |
| 30-argument row, clang | 4154 | 2794 | 2121 | **2081** |

Where it loses to `FMT_COMPILE`:

- A lone `%8.3f` is 22-52% slower, and the short mixed line, which contains one, 7-51% slower.
  static_format converts with `std::to_chars(value, fixed, precision)`, and libstdc++'s
  implementation of that is slower than fmt's own algorithm.
- A tiny result returned as `std::string` (`0x%02X`) takes 31-35 ns against fmt's 16-19.

It is ahead in every other case: integers into a buffer, the 30-argument row both ways, and the
mixed line returned as `std::string`. The `ascii_string_view` row is no faster than the plain
one beyond noise (at most 3%): scanning two 12-byte labels for non-ASCII bytes costs almost
nothing.
