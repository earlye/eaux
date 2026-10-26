# C++20 rewrite of formattype as c++/format/static-format

## Context

Early wants a modern C++20 rewrite of their old compile-time format library, `formattype`
(2019, C++17), to live in this repo at `c++/format/static-format`. That is Early's suggested
name. It matches the repo's `{language}/{area}/{package}` layout (`go/...`, `rust/ipc/app-agent`),
but this would be the first C++ tree here. Requested by the `stdplus` Claude session on Early's
behalf.

The original was written to replace `sprintf` in a profiled hot path. Usage:
`FORMAT("0x%02X") % 0xff`. It takes printf-style specs `%[N$][flags][width][.precision]spec`
and also boost-style `%N%`.

### How the original works

- The format string is lifted into a type with the vendored `irqus::typestring` macro, which
  caps strings at 128 chars and silently truncates longer ones.
- Segments are parsed by recursive template structs.
- `operator%` chains the arguments as a reverse linked list of pointers to temporaries.
- Output is a chain of segment writes through `std::ostream` manipulators, unrolled at compile
  time. Parsing therefore costs nothing at runtime. `std::format` is different: it validates at
  compile time (`consteval` `format_string`) but parses again at runtime.

### What the rewrite should do

- Replace typestring with a C++20 class-type NTTP `fixed_string<N>` (`template<fixed_string S>`),
  with no length cap. Also provide a UDL, `template<fixed_string S> operator""_fmt()`, so callers
  don't need a macro.
- Parse with ordinary `constexpr` functions over `S.data`. Use templates only where the result
  has to become a type, such as a tuple of segment descriptors.
- Store arguments by value in a `std::tuple`, not as pointers to temporaries.
- Check argument types against their specifiers at compile time; the original checks only the
  argument count. Mismatches should fail with `static_assert` messages rather than
  "no match for operator" errors.
- Consider converting numbers with `std::to_chars` instead of iostreams, since going through
  `ostream` may cancel out the speed advantage over `sprintf`.
- Add a benchmark against `sprintf`, `std::format`, and fmt's `FMT_COMPILE`.

### Gaps in the original to fix or decide on

- Centered alignment (`=`) is a TODO.
- The flags `+`, `#`, and space are parsed but never applied.
- The specifiers `A a C c F S` end a segment but have no handler, so using them fails to compile.
- A repeated positional placeholder (`%1% %1%`) consumes two arguments.
- An out-of-range positional index prints nothing and raises no error.
- Stream state such as `hex` carries over from one segment to the next.
- Storing the expression in `auto` leaves dangling pointers.

## Open questions

- Brace-style `{}` syntax: Early sees no reason not to support it alongside printf syntax, but
  wants to grill the exact syntax before implementing it.

## Prior art

- fmt's `FMT_COMPILE` / `_cf` literal is the closest analog.
- As far as we know, the standard has no compiled-format facility.

## Relevant files

These are in the `stdplus` repo, not this one:

- `/home/earlye/Documents/gogs/earlye/stdplus/include/stdplus/strings/formattype/`: the
  original implementation.
- `/home/earlye/Documents/gogs/earlye/stdplus/src/test/strings/test_format.cpp`: its tests.

## Grill Log

### 2026-10-26

- Q: What grammar should the brace syntax use? — A: `std::format`'s replacement-field grammar
  exactly: `{[arg-id][:[[fill]align][sign][#][0][width][.precision][type]]}`, with `{{`/`}}`
  escapes. Deferred for now: nested dynamic width/precision (`{:{}}`), chrono/range specs, and
  the `L` locale flag.
- Q: Build system for the repo's first C++ tree? — A: CMake, with strong PTSD-based
  reservations. Claude maintains it so Early doesn't have to. `c++/Makefile` wraps it (configure,
  build, `ctest` under both g++ and clang++), `c++` joins `LANGUAGES` in the root Makefile, and
  `.github/workflows/cpp.yml` runs a gcc/clang matrix path-filtered to `c++/**`. The library is a
  header-only `INTERFACE` target, so CMake exists only for tests and the benchmark.
- Q: Test framework and compile-fail tests? — A: doctest via CMake `FetchContent` (pinned tag)
  for runtime output tests; parse-level checks stay as plain `static_assert`s. Each promised
  `static_assert` diagnostic gets a one-file compile-fail test registered with CTest, passing only
  when compilation fails and stderr matches the expected message (`PASS_REGULAR_EXPRESSION`),
  under both g++ and clang++.
- Q: How does a format string choose printf vs brace syntax? — A: A separate literal suffix per
  syntax, each parsed by exactly one grammar: `"{:#04x}"_bfmt` for brace, `"0x%02X"_pfmt` for
  printf. Autodetection breaks on JSON-with-printf and on `100%` in brace strings; mixing both in
  one string would break each standard. `_fmt` is left alone because `std::` may claim it. Both
  suffixes share the segment representation and conversion code; only the parsers differ.
