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
`a5d8a13e3029` (the float conversion of ADR 0003, renderer forced inline):

| case | snprintf | std::format_to | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|---:|
| `0x%02X`, g++ 15.2 | 46.0 | 69.0 | 9.4 | 6.3 | **6.3** |
| `0x%02X`, clang 21.1 | 47.7 | 56.3 | 12.4 | **6.0** | 6.1 |
| `%8.3f`, g++ | 198 | 115 | 39.0 | **19.7** | 19.9 |
| `%8.3f`, clang | 200 | 122 | 42.6 | 21.6 | **21.6** |
| shortest `{}`, g++ | | 82.7 | **21.6** | | 29.8 |
| shortest `{}`, clang | | 71.9 | **25.6** | | 29.2 |
| `%g`, g++ | 182 | 114 | 32.8 | **18.8** | 18.9 |
| `%g`, clang | 184 | 122 | 34.6 | 23.4 | **22.9** |
| `%e`, g++ | 180 | 99.8 | 29.4 | **19.0** | 19.0 |
| `%e`, clang | 182 | 112 | 37.8 | **19.2** | 19.2 |
| float batch (48 fields), g++ | 9322 | 5737 | 1718 | **1008** | 1019 |
| float batch (48 fields), clang | 9336 | 5739 | 1792 | **912** | 914 |
| short mixed line, g++ | 307 | 263 | 56.7 | **47.7** | 49.2 |
| short mixed line, clang | 303 | 231 | 65.2 | 38.4 | **38.1** |
| 30-argument row, g++ | 5708 | 3890 | 1933 | **688** | 711 |
| 30-argument row, clang | 5697 | 4191 | 2274 | **677** | 692 |
| row, `ascii_string_view` labels, g++ | | | | 656 | 655 |
| row, `ascii_string_view` labels, clang | | | | 669 | 657 |

| returning `std::string` | std::format | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|
| `0x%02X`, g++ | 75.8 | 18.5 | **15.0** | 15.2 |
| `0x%02X`, clang | 66.7 | **19.5** | 19.8 | 19.7 |
| short mixed line, g++ | 286 | 166 | **87.8** | 90.6 |
| short mixed line, clang | 253 | 178 | 71.2 | **69.6** |
| 30-argument row, g++ | 3924 | 2468 | **843** | 885 |
| 30-argument row, clang | 4244 | 2952 | **867** | 971 |

Where it loses to `FMT_COMPILE`:

- Shortest `{}` on a double: 38% slower under g++, 14% under clang. Both run Dragonbox; the
  rest is static_format's layout, which reproduces `std::to_chars`' choice between fixed and
  scientific.
- A tiny result returned as `std::string` (`0x%02X`) is level under clang (1-2%); under g++ it
  is ahead.

It is ahead in every other case: integers into a buffer, every precision float field, the float
batch, and the 30-argument row and the mixed line both ways. The `ascii_string_view` row is 1-8%
faster than the plain one, within run-to-run noise for this row: scanning two 12-byte labels
for non-ASCII bytes costs almost nothing.

Profiling `%8.3f` on `std::to_chars` put about 80% of the time in libstdc++'s Ryu-printf
conversion (~580 instructions, more than fmt's whole field under g++). ADR 0003 records the
replacement and the alternatives measured. The vendored conversion halves the cost of every
precision float field (`%8.3f` 48 to 20 ns, about 320 instructions where libstdc++ took 660)
and puts static_format ahead of `FMT_COMPILE` there.

`to`, `render`, its segment lambda and `write_float` are forced inline
(`EAUX_STATIC_FORMAT_ALWAYS_INLINE`); the float conversion itself is not. Without the forced
inlining, clang calls the renderer out of line in many-field callers. With it, the float batch is
28% faster under clang and 17-21% under g++, and single fields stay within 3%. The cost is in
the `std::string` results: g++'s mixed line is about 25% slower with `_bfmt` and 6% with
`_pfmt`, and clang's `_bfmt` row about 10%.

The same on an Apple M4 Pro, macOS 15 (Darwin 24.6), CMake Release, Apple clang 17.0 with its
libc++ 20.1 and Homebrew clang 22.1 with libc++ 22.1, same fmt and Google Benchmark. macOS
cannot pin a process to a core, so these runs are unpinned. Commit
`3a0f66acca09cfb9a80e395530469ca16917ced5`, with the vendored float conversion:

| case | snprintf | std::format_to | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|---:|
| `0x%02X`, Apple clang 17.0 | 26.3 | 24.8 | 6.4 | 5.6 | **5.5** |
| `0x%02X`, clang 22.1 | 27.1 | 33.8 | **6.5** | 10.0 | 10.2 |
| `%8.3f`, Apple clang | 79.6 | 47.0 | 21.6 | 16.5 | **16.3** |
| `%8.3f`, clang | 82.2 | 48.0 | 21.1 | 11.4 | **11.2** |
| shortest `{}`, Apple clang | | 44.3 | **12.3** | | 26.3 |
| shortest `{}`, clang | | 48.1 | **12.2** | | 27.3 |
| `%g`, Apple clang | 70.1 | 56.6 | **16.9** | 17.8 | 17.4 |
| `%g`, clang | 71.7 | 59.3 | **17.0** | 17.6 | 17.9 |
| `%e`, Apple clang | 91.3 | 41.1 | 15.9 | **10.3** | 10.4 |
| `%e`, clang | 92.4 | 41.1 | 16.4 | 9.7 | **9.4** |
| float batch (48 fields), Apple clang | 3630 | 2427 | 899 | 565 | **564** |
| float batch (48 fields), clang | 3747 | 2482 | 883 | **581** | 589 |
| short mixed line, Apple clang | 148 | 105 | 31.4 | 26.2 | **26.1** |
| short mixed line, clang | 148 | 107 | 31.1 | 20.9 | **20.7** |
| 30-argument row, Apple clang | 2115 | 1493 | 1193 | 503 | **490** |
| 30-argument row, clang | 2183 | 1511 | 1158 | **324** | 337 |
| row, `ascii_string_view` labels, Apple clang | | | | 496 | **494** |
| row, `ascii_string_view` labels, clang | | | | **321** | 334 |

| returning `std::string` | std::format | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|
| `0x%02X`, Apple clang | 27.2 | **10.5** | 17.2 | 17.3 |
| `0x%02X`, clang | 35.7 | **9.6** | 15.6 | 13.3 |
| short mixed line, Apple clang | 122 | 85.1 | **47.2** | 47.4 |
| short mixed line, clang | 125 | 82.9 | **43.3** | 43.7 |
| 30-argument row, Apple clang | 1603 | 1722 | **565** | 571 |
| 30-argument row, clang | 1597 | 1616 | **433** | 434 |

Where it loses to `FMT_COMPILE`:

- Shortest `{}` on a double: about 2.1x under Apple clang and 2.2x under clang 22.1, a much
  wider gap than on Linux.
- `%g`: 3-4% slower under both compilers.
- The tiny `0x%02X`: about 55% (3.5 ns) slower into a buffer under clang 22.1, and 39-64%
  (4-7 ns) slower returned as `std::string` under both.

It is ahead in every other case. The vendored conversion puts `%8.3f` 25% (Apple clang) and 47%
(clang 22.1) ahead of `FMT_COMPILE`, where it was level before, and `%e` 35-43% ahead. The float
batch is 34-37% faster, the 30-argument row 59% (Apple clang) and 72% (clang 22.1) faster into a
buffer, and returned as `std::string` the row takes a quarter to a third of `FMT_COMPILE`'s time and
the mixed line a little over half.
