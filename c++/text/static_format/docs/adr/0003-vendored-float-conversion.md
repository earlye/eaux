# Floats are converted by vendored Dragonbox and a port of fmt's fixed-precision digits

Float fields are converted by code vendored under `detail/third_party/`, with no dependency:
Dragonbox (Junekey Jeon, BSL-1.0) for shortest form, and the Dragon4-free fast path of fmt
12.2.0's `format_float` (MIT; Jeon's algorithm, using Dragonbox's cache plus fmt's 15 extra
entries for subnormals) for `%f`, `%e` and `%g`. `float_chars.hpp` lays the digits out so the
output equals `std::to_chars` byte for byte, including its shortest-form choice between fixed
and scientific and its exact printing of integers from 2^53. Sign, padding, `#` and upper case
stay in `write_float`. `long double` wider than `double`, `%a`, and what the fast path cannot
decide go to `std::to_chars`: more than 17-18 significant digits in total, which covers `%e` at
precision 17 and up (at 17 only for some values), `%g` at 18 and up, and `%f` where integral
digits plus precision exceed that (e.g. `%.6f` of values from 1e12).
`float` converts as `double`, except shortest form, which uses Dragonbox's binary32 mode.

The rule was fixed before measuring: vendored wins if it matches `std::to_chars` digit for digit
and is within ~5% of an fmt backend on every float row under g++ and clang; otherwise fmt ships
as an optional backend over the faster of the rest.

Median ns (callgrind instructions) per call, Ryzen 9 3900X pinned to one core, three interleaved
rounds of three repetitions each, fmt 12.2.0, `_bfmt` rows (`_pfmt` is the same within noise):

| row | `std::to_chars` | fmt backend | floff | vendored | fmt FMT_COMPILE itself |
|---|---:|---:|---:|---:|---:|
| `%8.3f`, g++ 15.2 | 47.7 (660) | 64.2 (725) | 35.2 (461) | **20.0 (321)** | 38.0 (543) |
| `%8.3f`, clang 21.1 | 50.3 (668) | 58.0 (784) | 31.8 (426) | **21.0 (292)** | 43.6 (616) |
| shortest `{}`, g++ | 39.4 (597) | **28.3 (376)** | 29.5 (384) | 29.6 (383) | 21.7 (277) |
| shortest `{}`, clang | 38.2 (604) | **27.1 (342)** | 28.2 (353) | 28.3 (353) | 25.5 (347) |
| `%g`, g++ | 52.6 (714) | 51.2 (645) | 28.3 (387) | **19.1 (303)** | 31.9 (498) |
| `%g`, clang | 53.4 (719) | 53.3 (709) | 30.8 (427) | **23.9 (344)** | 34.5 (553) |
| `%e`, g++ | 41.1 (608) | 46.4 (613) | 18.9 (289) | **18.9 (284)** | 28.9 (463) |
| `%e`, clang | 40.2 (613) | 52.2 (657) | **16.8 (256)** | 18.9 (267) | 38.4 (502) |
| float batch, g++ | 2535 (31624) | 2722 (32155) | 1754 (20358) | **1286 (17444)** | 1812 (23966) |
| float batch, clang | 2631 (31345) | 2514 (32179) | 1640 (18691) | **1267 (15165)** | 1958 (25381) |
| mixed line, g++ | 79.6 (971) | 88.9 (1023) | 65.1 (749) | **49.7 (643)** | 54.9 (799) |
| mixed line, clang | 64.3 (908) | 76.9 (1008) | 50.2 (656) | **37.4 (525)** | 71.4 (1036) |
| 30-argument row, g++ | 1504 (19299) | 1814 (21468) | 1214 (13848) | **718 (10463)** | 1951 (21924) |
| 30-argument row, clang | 1535 (19283) | 1781 (22851) | 989 (12471) | **716 (8859)** | 2330 (26026) |

The batch rows predate forcing the renderer glue inline (`EAUX_STATIC_FORMAT_ALWAYS_INLINE`);
with that, the vendored batch is 1019 ns under g++ and 914 under clang (`bench.jsonl`).

Vendored is 40-69% faster than the fmt backend on every row but shortest form, where it is 4.4%
(clang) and 4.6% (g++) slower, 2-3% more instructions: both run Dragonbox and the same layout
code, and fmt's copy of Dragonbox is slightly leaner. That is inside the ~5%, so vendored wins.
It is also faster than fmt's own `FMT_COMPILE` on every row except shortest form.

## Considered Options

- fmt as a backend (required or optional). Rejected by the rule above. Handing it the numeric
  conversion (`FMT_COMPILE("{:.{}f}")`, a runtime precision because `%#g` picks one at run time)
  is slower than `std::to_chars` for `%f` and `%e`: fmt formats into a `memory_buffer` and walks
  its own spec handling before the digits. It would also add a link dependency.
- floff (Jeon) for fixed precision, the other vendored variant. Licence fine (BSL-1.0), but its
  README calls it a test repository for a work-in-progress algorithm, its last fixes landed in
  2024 and 2026, and the digit test found an upstream bug: the smallest subnormals at precision
  1 print as `0.0e-324`. It is also slower than the fmt port everywhere except clang's `%e`.
- `std::to_chars` alone. libstdc++'s precision path is Ryu-printf, about 600
  instructions per field; it remains the fallback.
- Ryu-printf vendored. Not tried: it is what libstdc++ already runs.

## Consequences

About 270 KB of vendored headers (Dragonbox is 260 KB of it, much of that its cache tables), each with
its upstream licence text. A Dragonbox or fmt fix has to be copied in by hand. `float_chars_test`
checks the conversion against `std::to_chars` for `%f`, `%e` and `%g` at precisions 0-17 and
selected larger ones up to 1074, for `%#g` at 0-17 and for shortest form, over random and
edge-case values, and checks that no conversion writes past its capacity, so a port mistake or
an upstream regression shows up as a test failure.
