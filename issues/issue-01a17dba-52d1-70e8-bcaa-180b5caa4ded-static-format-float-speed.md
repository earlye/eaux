# static_format: close the float gap with fmt's FMT_COMPILE

## Context

The benchmark in `c++/text/static_format/README.md` compares static_format against fmt's
`FMT_COMPILE`. static_format is ahead everywhere except floats:

- **A lone `%8.3f`:** 21–28% slower (about 52 ns against fmt's 41–42 ns). Timing splits from
  PR #7 put libstdc++'s `std::to_chars(value, fixed, 3)` alone at about 40 ns, against 31 ns
  (g++) and 42 ns (clang) for fmt's whole field, with about 10 ns of static_format's own on top,
  half of it width padding.
- **Probably shortest-form floats** (`{}` on a double, `%g` with no precision). Not measured yet.

The gap must close: scientific output writes thousands upon thousands of floats, so ~10 ns per
field adds up. Under clang the numbers above suggest the conversion is roughly level with fmt
and the loss is mostly our own overhead, which no conversion backend would remove. So the work
starts with a profile, not a backend.

## Plan

### 1. Benchmark rows

Add rows before measuring anything: shortest `{}` on a double, `%g` without precision, `%e`, and
a large batch of mixed floats (the scientific-output case).

### 2. Profile the current code

Both `perf` and `valgrind` are installed and usable on the dev machine.

- `perf record -g` / `perf annotate` on the bench binary for `%8.3f` and the short mixed line,
  under g++ and clang: split the time between conversion and static_format's overhead.
- callgrind for exact instruction counts (sampling is noisy at ~50 ns per field).
- Candidate overheads to confirm or refute, from reading `write_float` in `detail/write.hpp`:
  the copy from the 128-byte stack buffer through `put_padded`, and the `signbit`/`isfinite`/
  `isnan` checks ahead of the conversion.
- Fix any overhead the profile finds that is independent of the backend.
- Record the findings in the README.

### 3. Candidate backends

Selected at compile time and benchmarked side by side:

- **`std::to_chars`**: today's path, the baseline.
- **fmt**: hand float fields to fmt's compiled formatting. static_format still parses,
  type-checks and validates at compile time; the printf flags (`#`, `+`, space, `0`, and the
  `^ = >` alignments) are translated to fmt specs.
- **Vendored**: header-only, no dependency. Dragonbox (Junekey Jeon; Apache-2.0 WITH
  LLVM-exception or BSL-1.0, both MIT-compatible) for shortest form. For fixed precision, two
  variants, and the profiler picks between them:
  - a port of fmt's fixed-precision digit generation (MIT);
  - Junekey Jeon's floff, if a licence check finds it usable under MIT. If not, only the fmt
    port enters.

  Libstdc++'s precision path is believed to be Ryu-printf already (unverified), so Ryu is not a
  candidate.

Out of scope for every backend: x87 80-bit `long double` and `%a` stay on `std::to_chars`.
`float` is handled like `double`.

Only numeric fields can go to fmt. String width and precision count grapheme clusters and display
width (ADR 0002), so strings stay ours. `{:c}` on an integer writes a code point (ADR 0002) and
must not go to fmt either.

### 4. Correctness

Every non-baseline backend matches `std::to_chars` digit for digit across randomized doubles and
precisions 0–17, for `%f`, `%e`, `%g` and shortest form.

### 5. Decision rule (set before measuring)

- **Vendored wins** if it is correct and within ~5% of fmt on every float row under both g++ and
  clang. The losing vendored variant is deleted.
- **Otherwise fmt wins** and ships as an *optional* backend (CMake option), with the faster of
  vendored and `std::to_chars` as the fallback, so the dependency-free build stays.
- The `std::to_chars` path survives only as that fallback. Backends that lose are deleted before
  the PR.
- The outcome is hard to reverse and the rejected alternatives are non-obvious: write an ADR in
  `c++/text/static_format/docs/adr/` recording the measurements and the choice.

## Relevant files

- `c++/text/static_format/README.md`: benchmark tables and the list of losses.
- `c++/text/static_format/include/eaux/text/static_format/detail/write.hpp`: `write_float`,
  `float_chars`, `shortest_chars`, `put_padded`.
- `c++/text/static_format/bench/`: benchmark harness (Google Benchmark plus fmt through
  `FetchContent`).
- `c++/text/static_format/docs/adr/0002-utf8-width-and-truncation.md`: the text width rules and
  the `{:c}` difference that must stay ours.

## Grill Log

### 2026-10-27

- Q: Is the ~10 ns `%8.3f` gap worth closing, given that only option B (fmt) closes it as
  written? — A: Yes. Scientific output writes thousands of floats, so the gap adds up. How to
  close it (fmt dependency, optional backend, or our own implementation) is still open.
- Q: Depend on fmt (required or optional), or vendor an algorithm? — A: Implement the
  candidates and let profiling pick. Required vs optional fmt is a packaging choice that
  profiling cannot separate, so the experiment has three compile-time backends: `std::to_chars`
  (baseline), fmt, and vendored (Dragonbox for shortest form plus a fixed-precision algorithm).
- Q: What decides the winner? — A: Vendored wins if it matches `std::to_chars` digit for digit
  (randomized doubles, precisions 0–17) and is within ~5% of fmt on every float row under both
  g++ and clang. Otherwise fmt ships as an optional backend with the faster of the others as
  fallback. The `std::to_chars` path survives only as that fallback. New benchmark rows first:
  shortest `{}` on a double, `%g` without precision, `%e`, and a large batch of mixed floats.
- Q: Why are we losing? — A: Not yet known; the PR #7 numbers are timing splits, not a profile.
  Profile before building backends: `perf record -g`/`perf annotate` on the `%8.3f` and short
  mixed line rows under g++ and clang, plus callgrind for exact instruction counts. Both tools
  are installed and usable. Candidate overheads to confirm or refute: the copy from the 128-byte
  stack buffer through `put_padded`, and the sign/finite/NaN checks ahead of the conversion.
  Under clang the README's numbers suggest the conversion is roughly level with fmt and the
  ~10 ns is ours, which no backend would remove.
- Q: Which fixed-precision algorithm for the vendored backend: port fmt's, floff, or both? —
  A: Profiler decides. Both enter the run if floff's licence and maturity allow; the loser is
  deleted.
