# eaux::text::unicode

UTF-8 text measurement: extended grapheme cluster segmentation (UAX #29), display width as
`std::format` estimates it ([format.string.std]), and a word-wise ASCII scan. Useful for column
layout and for truncating labels without cutting a character in half. Header-only, C++23,
everything `constexpr`, CMake target `eaux::text::unicode`.

```cpp
#include <eaux/text/unicode/unicode.hpp>

namespace u = eaux::text::unicode;

u::display_width("σ_max");             // 5
u::display_width("中文");           // 4: East Asian wide characters count 2
u::width_prefix("ééé", 2); // {size: 6, width: 2}: never splits a cluster
u::byte_prefix(label, 16);                   // longest whole-cluster prefix within 16 bytes
u::ascii_prefix(s);                          // offset of the first byte >= 0x80
for (std::string_view g : u::graphemes(s)) { /* one cluster at a time; a borrowed view */ }
```

Width is the sum, over clusters, of the width of each cluster's first code point: 2 for
East_Asian_Width W or F and the standard's three extra pictograph ranges, otherwise 1. ASCII
text takes a fast path where width equals length. Malformed UTF-8 is never rejected: each bad
byte counts as one U+FFFD.

## Unicode tables

`include/eaux/text/unicode/detail/tables.hpp` is generated and checked in, pinned to Unicode
18.0.0. To move to a new version, run

```sh
python3 scripts/generate_tables.py --version 19.0.0
```

It downloads the UCD files to a temporary directory (or to `DIR/<version>` with `--cache DIR`),
refuses any file that declares a different version, rewrites the header, and refreshes
`tests/data/GraphemeBreakTest.txt`, which the tests run in full.
