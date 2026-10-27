# static_format: close the float and small-string gap with fmt's FMT_COMPILE

## Context

The benchmark in `c++/text/static_format/README.md` compares static_format against fmt's
`FMT_COMPILE`. static_format is ahead on `0x%02X` and on the ~30-argument table row. It is behind
on:

- **A lone `%8.3f`:** 22–52% slower. The cost is libstdc++'s `std::to_chars` with a precision.
- **Probably shortest-form floats** (`{}` on a double, `%g` with no precision). These aren't
  measured separately yet.

Early asked whether static_format could use Dragonbox the way fmt does, and suggested an
alternative: for the cases where `FMT_COMPILE` is faster, wrap it. static_format would then
contribute the compile-time safety (printf syntax, `static_assert` diagnostics, the rule that
every argument is used) and fmt the speed.

This is tracked on the PR #7 branch (`earlye/273c554893e7/cpp20-static-format-rewrite`) so it
merges with that work. Profiling `%8.3f` and the small-string fix are going into PR #7 itself.
Their findings should feed the decision below.

## Options

### A. Vendor Dragonbox

- **What it is:** Junekey Jeon's algorithm for the shortest round-trip representation of a
  floating-point value. The reference implementation is a single header, licensed Apache-2.0
  WITH LLVM-exception or BSL-1.0, either of which is compatible with this MIT repo.
- **What it would speed up:** the shortest-form path only (`{}` on doubles, `%g` with no
  precision).
- **What it would not speed up:** fixed-precision formatting such as `%8.3f`, because there is
  no shortest form to find. fmt handles that with its own digit generation and a big-integer
  fallback (unverified). Closing the `%8.3f` gap needs a separate approach.

### B. Hand fields to fmt's compiled formatting (Early's idea)

For argument and spec combinations where `FMT_COMPILE` measures faster, format the field
through fmt. static_format would still parse, type-check and validate at compile time.

Trade-offs:

- **Dependency:** fmt becomes a hard dependency of the library. Today it is only used by the
  benchmark.
- **Only numeric fields can be handed over.** For text, static_format's width and precision
  count grapheme clusters and display width (ADR 0002). fmt's rules differ, so string fields
  must stay ours. Numbers are ASCII, so their padding behaves the same either way.
- **printf specs need translating** to fmt's brace specs, flags included (`#`, `+`, space,
  `0`, and the `^ = >` alignments).
- **Deliberate differences must stay ours.** `{:c}` on an integer writes a code point
  (ADR 0002), and fmt must not take over that case.
- **The benchmark becomes partly circular** for the cases handed over.

## Next steps

- Choose A, B or both once the `%8.3f` profiling from PR #7 is in.
- For B, decide whether fmt becomes a required dependency or an optional backend (a CMake
  option, falling back to `std::to_chars`).

## Relevant files

- `c++/text/static_format/README.md`: benchmark tables and the list of losses.
- `c++/text/static_format/include/eaux/text/static_format/detail/write.hpp`: float writing,
  `float_chars` and `shortest_chars`.
- `c++/text/static_format/bench/`: benchmark harness (Google Benchmark plus fmt through
  `FetchContent`).
- `c++/text/static_format/docs/adr/0002-utf8-width-and-truncation.md`: the text width rules and
  the `{:c}` difference that option B must keep.

## Grill Log

### 2026-10-27

- Q: Is the ~10 ns `%8.3f` gap worth closing, given that only option B (fmt) closes it as
  written? — A: Yes. Scientific output writes thousands of floats, so the gap adds up. How to
  close it (fmt dependency, optional backend, or our own implementation) is still open.
