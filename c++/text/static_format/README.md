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
through. Integers go through `std::to_chars`; floats through vendored Dragonbox and a port of
fmt's fixed-precision digits (ADR 0003), which write exactly what `std::to_chars` writes, so
output is always "C"-locale and needs no dependency. MSVC builds need
`/utf-8`, which the CMake target adds. See `docs/adr/`.

## Benchmarks

`make bench` (from `c++/`) builds `bench/` optimized with each compiler and runs it. CI only
builds it. Each case writes into a preallocated buffer; the `string_` cases return
`std::string`. The `row` cases format a 30-argument table row (two `%-12s` labels and 28
`%10.4f`), and `row_ascii` passes the labels as `ascii_string_view`. The shortest `{}`, `%g` and
`%e` cases and the float batch take 17-digit values from 1e-9 to 1e9; the batch writes 16 lines
of `"%.6e %g %10.4f\n"` (48 float fields) per call. printf has no shortest form, so `_pfmt` and
snprintf have no shortest case.

`bench/bench.jsonl` is the benchmark history: one JSON object per commit, compiler and case,
with the host, the compiler, whether the run was pinned, and each contender's median ns.
`make bench-record` (or `python3 bench/record.py` where make is missing) benchmarks the
committed tree with every compiler it finds and appends to it; it refuses while tracked files
have changes, so every row is tied to the exact commit measured.

Median ns per call on an AMD Ryzen 9 3900X, Linux 7.0, CMake Release (`-O3`), libstdc++ 15,
fmt 12.2.0, Google Benchmark 1.10.0, pinned to one core, 5 repetitions. These are the rows that
`make bench-record` appended to `bench/bench.jsonl` for commit
`a94e0c888d61` (the float conversion of ADR 0003):

| case | snprintf | std::format_to | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|---:|
| `0x%02X`, g++ 15.2 | 44.8 | 68.9 | 9.3 | **7.9** | 8.3 |
| `0x%02X`, clang 21.1 | 45.4 | 55.8 | 12.0 | **6.0** | 6.0 |
| `%8.3f`, g++ | 197 | 117 | 37.8 | 19.8 | **19.8** |
| `%8.3f`, clang | 199 | 118 | 43.6 | **20.7** | 20.8 |
| shortest `{}`, g++ | | 94.2 | **21.6** | | 29.3 |
| shortest `{}`, clang | | 71.0 | **25.4** | | 28.0 |
| `%g`, g++ | 181 | 114 | 31.3 | **18.9** | 19.0 |
| `%g`, clang | 184 | 120 | 34.3 | **22.5** | 23.8 |
| `%e`, g++ | 179 | 103 | 28.9 | 19.0 | **18.8** |
| `%e`, clang | 183 | 109 | 38.2 | 18.8 | **18.6** |
| float batch (48 fields), g++ | 9282 | 5687 | 1795 | **1197** | 1281 |
| float batch (48 fields), clang | 9272 | 5719 | 1950 | **1262** | 1266 |
| short mixed line, g++ | 305 | 252 | 55.1 | 55.9 | **48.8** |
| short mixed line, clang | 301 | 231 | 71.3 | 38.1 | **37.0** |
| 30-argument row, g++ | 5660 | 3902 | 1909 | **709** | 715 |
| 30-argument row, clang | 5696 | 4172 | 2310 | **704** | 710 |
| row, `ascii_string_view` labels, g++ | | | | 712 | 660 |
| row, `ascii_string_view` labels, clang | | | | 699 | 699 |

| returning `std::string` | std::format | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|
| `0x%02X`, g++ | 72.0 | 17.7 | **14.7** | 14.8 |
| `0x%02X`, clang | 68.0 | **19.1** | 21.1 | 21.2 |
| short mixed line, g++ | 269 | 164 | 82.6 | **72.6** |
| short mixed line, clang | 254 | 159 | **74.5** | 75.0 |
| 30-argument row, g++ | 3947 | 2315 | 881 | **879** |
| 30-argument row, clang | 4278 | 2802 | 898 | **884** |

Where it loses to `FMT_COMPILE`:

- Shortest `{}` on a double: 35% slower under g++, 10% under clang. Both run Dragonbox; the
  rest is static_format's layout, which reproduces `std::to_chars`' choice between fixed and
  scientific.
- `_pfmt`'s mixed line under g++ is level (1%); `_bfmt` is ahead.
- A tiny result returned as `std::string` (`0x%02X`) is about 2 ns (10%) slower under clang;
  under g++ it is ahead.

Where the float time goes, from `perf record` and callgrind on `%8.3f` (libstdc++ debug symbols
from debuginfod): about 80% is libstdc++'s conversion under both compilers. Its precision path
is Ryu-printf: `d2fixed_buffered_n` takes 64% of the samples and the `__floating_to_chars_precision`
wrapper 14%. That is about 580 instructions per call, more than fmt's whole `{:8.3f}` field
under g++ (558). static_format's own share was about 10 ns (90-110 instructions), spread over
frame setup, lambda captures, the `memcpy` of the digits and the padding. The `signbit`/`isfinite`
checks cost under 0.5 ns. Inlining the short copies and fills in place of the `memcpy`/`memset`
calls was slower (g++ `%8.3f` 50 to 59 ns). Moving the heap fallback out of `write_float`
and folding the fill at compile time cut 2-5% from the float fields and 5-16% from the mixed
line and the row. Clang's batch got about 7% slower, because clang then stops inlining the
segment render into the loop.

So under g++ only a faster conversion could close the gap. ADR 0003 records the three
candidates measured side by side: an fmt backend, vendored Dragonbox plus a port of fmt's
fixed-precision digits, and the same with floff. The vendored port won: it halves the cost of
every precision float field (`%8.3f` 48 to 20 ns, about 320 instructions where libstdc++ took
660) and puts static_format ahead of `FMT_COMPILE` there. With it, a1bc91a's clang regression
remains and is larger: the batch is 20-25% slower than with a1bc91a reverted, while the single
fields are 3-8% faster with it.

It is ahead in every other case: integers into a buffer, every precision float field, the float
batch, the 30-argument row both ways, and the mixed line returned as `std::string`. The `ascii_string_view` row is no faster than the plain
one beyond noise (at most 4%): scanning two 12-byte labels for non-ASCII bytes costs almost
nothing.
