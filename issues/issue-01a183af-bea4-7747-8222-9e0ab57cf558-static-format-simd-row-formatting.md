# static_format: investigate SIMD for formatting rows of fixed-precision fields

## Context

Early wants to find out whether SIMD can speed up static_format on rows of data, such as
`%8.3f %8.3f %8.3f %8.3f`. Their sketch of the idea:

- Preallocate the output buffer.
- Load all the parameters.
- Format the fields of the row in parallel instead of one after another.

static_format knows the whole format string at compile time. For a row like this it knows every
field's spec and its fixed width, so each field's place in the output may be computable before
any conversion starts. Fields can widen when a value needs more than 8 characters, so that
can't be assumed.

### Current state

Today every float field is converted on its own with `std::to_chars` (`float_chars` in
`detail/write.hpp`). From the README benchmark (Ryzen 9 3900X, commit `5a5db785`):

- **A lone `%8.3f`:** 52.4 ns (g++) and 51.7 ns (clang). Of that, about 40 ns is libstdc++'s
  `std::to_chars(value, fixed, 3)`, and about 10 ns is static_format's own work, half of it
  width padding.
- **The 30-argument row case** (two `%-12s` labels and 28 `%10.4f`): 1741 ns (g++) and
  1488 ns (clang) into a buffer. That's roughly 53–62 ns per float field. This case is the
  natural baseline for a row experiment.

## Related

- `issues/issue-01a17dba-52d1-70e8-bcaa-180b5caa4ded-static-format-float-speed.md` targets the
  same cost, the per-field fixed-precision float conversion, by vendoring Dragonbox or handing
  number fields to fmt. A SIMD row path would replace per-field conversion for the cases it
  covers, so the two approaches may overlap or compete. Neither blocks the other.

## Relevant files

- `c++/text/static_format/include/eaux/text/static_format/detail/write.hpp:199-210`:
  `float_chars` and `shortest_chars`, the current per-field float conversion.
- `c++/text/static_format/include/eaux/text/static_format/detail/sink.hpp`: output buffer
  handling.
- `c++/text/static_format/bench/bench.cpp`: benchmark cases, including the 30-argument row.
- `c++/text/static_format/README.md:95-148`: benchmark results and where static_format loses
  to `FMT_COMPILE`.
