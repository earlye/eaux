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

`bench/bench.jsonl` is the benchmark history: one JSON object per commit, compiler and case,
with the host, the compiler, whether the run was pinned, and each contender's median ns.
`make bench-record` (or `python3 bench/record.py` where make is missing) benchmarks the
committed tree with every compiler it finds and appends to it; it refuses while tracked files
have changes, so every row is tied to the exact commit measured.

Median ns per call on an AMD Ryzen 9 3900X, Linux 7.0, CMake Release (`-O3`), libstdc++ 15,
fmt 12.2.0, Google Benchmark 1.10.0, pinned to one core, 5 repetitions. These are the rows that
`make bench-record` appended to `bench/bench.jsonl` for commit
`5a5db785e922aad59bc1028a68d8848827ad7812`:

| case | snprintf | std::format_to | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|---:|
| `0x%02X`, g++ 15.2 | 44.9 | 65.6 | 9.6 | 8.7 | **8.6** |
| `0x%02X`, clang 21.1 | 47.0 | 54.5 | 12.3 | **6.1** | 6.1 |
| `%8.3f`, g++ | 206 | 117 | **40.8** | 52.4 | 52.1 |
| `%8.3f`, clang | 210 | 123 | **42.5** | 51.7 | 51.6 |
| short mixed line, g++ | 304 | 253 | **57.2** | 77.3 | 84.5 |
| short mixed line, clang | 302 | 228 | **62.1** | 65.0 | 65.0 |
| 30-argument row, g++ | 5838 | 3867 | 1919 | **1741** | 1745 |
| 30-argument row, clang | 5886 | 4056 | 2239 | 1488 | **1476** |
| row, `ascii_string_view` labels, g++ | | | | 1735 | 1745 |
| row, `ascii_string_view` labels, clang | | | | 1502 | 1500 |

| returning `std::string` | std::format | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|
| `0x%02X`, g++ | 73.2 | 16.6 | **15.8** | 16.1 |
| `0x%02X`, clang | 64.4 | **19.1** | 21.0 | 21.0 |
| short mixed line, g++ | 270 | 153 | 107 | **106** |
| short mixed line, clang | 253 | 167 | 110 | **108** |
| 30-argument row, g++ | 3879 | 2278 | **1757** | 1775 |
| 30-argument row, clang | 4133 | 2833 | 1676 | **1645** |

Where it loses to `FMT_COMPILE`:

- A lone `%8.3f` is 21-28% slower, and the short mixed line into a buffer, which contains one,
  5% slower under clang and 35-48% under g++. Most of the time is the conversion: libstdc++'s
  `std::to_chars(value, fixed, 3)` alone takes about 40 ns here, against fmt's 31 ns (g++) and
  42 ns (clang) for the whole field; static_format adds about 10 ns of its own, half of it the
  width padding.
- A tiny result returned as `std::string` (`0x%02X`) is about 2 ns (10%) slower under clang;
  under g++ it is level or ahead.

It is ahead in every other case: integers into a buffer, the 30-argument row both ways, and the
mixed line returned as `std::string`. The `ascii_string_view` row is no faster than the plain
one beyond noise (at most 2%): scanning two 12-byte labels for non-ASCII bytes costs almost
nothing.

The same on an Apple M4 Pro, macOS 15 (Darwin 24.6), CMake Release, Apple clang 17.0 with its
libc++ 20.1 and Homebrew clang 22.1 with libc++ 22.1, same fmt and Google Benchmark. macOS
cannot pin a process to a core, so these runs are unpinned. Commit
`b4862f2fa7d840a8e96e63cd749903588b31129a`:

| case | snprintf | std::format_to | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|---:|
| `0x%02X`, Apple clang 17.0 | 26.6 | 25.2 | 6.5 | **5.3** | 5.5 |
| `0x%02X`, clang 22.1 | 27.2 | 27.3 | **6.6** | 9.9 | 9.7 |
| `%8.3f`, Apple clang | 83.0 | 47.8 | **22.0** | 22.8 | 22.3 |
| `%8.3f`, clang | 85.9 | 48.2 | 21.7 | **20.8** | 21.4 |
| short mixed line, Apple clang | 146 | 106 | **32.0** | 32.9 | 32.8 |
| short mixed line, clang | 152 | 111 | 33.2 | **33.2** | 33.8 |
| 30-argument row, Apple clang | 2153 | 1486 | 1193 | **693** | 696 |
| 30-argument row, clang | 2240 | 1511 | 1193 | **707** | 714 |
| row, `ascii_string_view` labels, Apple clang | | | | 693 | 687 |
| row, `ascii_string_view` labels, clang | | | | 691 | 717 |

| returning `std::string` | std::format | fmt FMT_COMPILE | `_pfmt` | `_bfmt` |
|---|---:|---:|---:|---:|
| `0x%02X`, Apple clang | 27.0 | **10.3** | 14.4 | 15.5 |
| `0x%02X`, clang | 29.1 | 9.9 | **9.9** | 10.0 |
| short mixed line, Apple clang | 121 | 82.7 | **55.5** | 56.4 |
| short mixed line, clang | 127 | 82.5 | 56.1 | **55.8** |
| 30-argument row, Apple clang | 1592 | 1659 | **823** | 824 |
| 30-argument row, clang | 1615 | 1619 | **828** | 829 |

Here `%8.3f` and the short mixed line are within 4% of `FMT_COMPILE` either way. It loses on the
tiny `0x%02X`: about 50% (3 ns) slower into a buffer under clang 22.1, and about 40% (4 ns)
slower returned as `std::string` under Apple clang; with the other compiler it is level or
ahead in each. The 30-argument row is 40-42% faster into a buffer and about half the time
returned as `std::string`, and the mixed line returned as `std::string` about a third faster.
