# C++23 rewrite of formattype as c++/text/static_format

## Context

Early wants a modern C++ rewrite of their old compile-time format library, `formattype`
(2019, C++17), to live in this repo at `c++/text/static_format`, namespace `eaux::text::static_format`. (Early
first suggested `c++/format/static-format`; the grill moved it under `text` and matched the
directory to the namespace.) This is the first C++ tree here, and the issue also creates two
sibling packages, `c++/text/ascii` and `c++/text/unicode`. Requested by the `stdplus` Claude session on Early's
behalf. (The work started out as a C++20 rewrite, which is why the branch and filename still say
`cpp20`; the grill moved it to C++23.)

The original was written to replace `sprintf` in a profiled hot path. Usage:
`FORMAT("0x%02X") % 0xff`. It takes printf-style specs `%[N$][flags][width][.precision]spec`
and also boost-style `%N%`.

### How the original works

- The format string is lifted into a type with the vendored `irqus::typestring` macro, which
  caps strings at 128 chars and silently truncates longer ones.
- Segments are parsed by recursive template structs.
- `operator%` chains the arguments as a reverse linked list of pointers to temporaries. The
  resulting lazy object becomes text through implicit `operator std::string`, `operator==`,
  `operator+` or `<<`.
- Output is a chain of segment writes through `std::ostream` manipulators, unrolled at compile
  time. Parsing therefore costs nothing at runtime. `std::format` is different: it validates at
  compile time (`consteval` `format_string`) but parses again at runtime. fmt's `FMT_COMPILE`
  already does the compile-time-parse trick, so it is the bar the benchmark has to clear.

## Design

### Syntax and API

- The format string is a C++ class-type NTTP, `fixed_string<N>`, with no length cap.
- There are two literal suffixes, one per syntax, each parsed by exactly one grammar.
  `_fmt` is avoided because `std::` may claim it.
  - `"{:#04x}"_bfmt` is brace syntax: `std::format`'s replacement-field grammar, i.e.
    `{[arg-id][:[[fill]align][sign][#][0][width][.precision][type]]}` with `{{`/`}}` escapes.
    Nested dynamic width/precision and `L` are deferred. Chrono and range specs work through the
    `std::formatter` fallback for those types.
  - `"0x%02X"_pfmt` is printf syntax plus boost-style `%N%`.
- The literals live in `eaux::text::static_format::literals`.
- Calls are eager:
  - `"..."_pfmt(a, b, ...)` returns `std::string`.
  - `.to(out_it, args...)` writes to an iterator.
  - `.to_n(out_it, n, args...)` returns `{out, size}`, where `size` is the untruncated length.
- `%` chaining is gone, and with it the lazy object and its dangling pointers.
- Dozens of arguments must work, for tabular output of structural analysis data.

### Parsing and expansion

- Parse with `constexpr` functions into a `constexpr` array of segment descriptors.
- Expand over arguments with `std::index_sequence` and fold expressions.
- Never recurse per segment or per argument. That keeps long strings and many arguments clear of
  template-depth limits.

### printf-side type checking (compile time, `static_assert` naming argument index, specifier and type)

| Specifier | Accepts |
|---|---|
| `d i` | any integral type except `bool` and the char types |
| `u o x X` | any integral type; negatives are cast to unsigned, as printf does |
| `f F e E g G a A` | floating-point types |
| `c` | char types only (an `int` needs a cast) |
| `s` | string-like: `char const*`, `std::string`, `std::string_view`, `eaux::text::ascii_string_view`; `%{ascii}s` requires the last |
| `p` | pointers |
| `v` | a user type through `std::formatter<T>`, else `operator<<`, else an error naming both |
| `V` | a user type through `eaux::text::static_format::formatter<T>`, with an optional compile-time-parsed spec placed before the letter: `%[N$][flags][width][.precision][{spec}]V` |

Further printf rules:

- Length modifiers (`hh h l ll z j t L`) are accepted and ignored.
- `C S` are compile errors that explain they are deprecated wide forms.
- For `v` and `V`, width and the alignment flags `- ^ >` pad the output, precision truncates,
  and other flags are errors. See ADR 0001.
- Any built-in specifier may take a braced spec before its letter, and unknown keys are compile
  errors. The only key so far is `ascii` on `%s`.
- Flags:
  - `- + # space 0` have C semantics.
  - Alignment extensions are `^` center, `=` internal, and `>` as explicit right-align.
  - The original's `_` is dropped, and `=` no longer means center.
- A repeated positional (`%1%`, `%1$s`) reuses its argument.
- An out-of-range index, and mixing positional with sequential arguments, are compile errors.

The brace side follows `std::format`'s type rules. For `{:spec}` on a user type it tries, in
order:

1. `eaux::text::static_format::formatter<T>`
2. `std::formatter<T>`
3. `operator<<`, only when the spec is empty

`eaux::text::static_format::formatter<T>` provides:

- `static constexpr Spec parse(std::string_view)`, where `Spec` is a structural type and an
  invalid spec fails the build
- a runtime `format(spec, out, value)`

### Conversion, text and locale

- Numbers are converted with `std::to_chars`, not iostreams; only the `operator<<` fallback goes
  through a stream, imbued with the classic locale.
- Output is always "C"-locale.
- Text is UTF-8 everywhere, in both syntaxes, and the unit is the extended grapheme cluster
  (UAX #29):
  - Width counts display width, using the standard's wide ranges.
  - String precision never splits a grapheme.
  - `to_n` stops at a grapheme boundary.
- How the text handling is implemented:
  - `to_n` formats the complete output into the stack-buffered sink, then copies the longest
    prefix ending on a grapheme boundary, so a cluster spanning a literal and an argument is
    never split.
  - Runtime strings get a word-wise up-front scan for bytes >= 0x80. ASCII strings take the
    length-equals-width fast path, so there is no opt-out mode.
  - Malformed UTF-8 passes through.
  - `eaux::text::ascii_string_view` lives in its own package (see below) and validates once
    at construction: `consteval` from `"…"_ascii` (in `eaux::text::literals`), or
    `ascii_string_view::from(sv)` returning
    `std::expected<ascii_string_view, std::size_t>` (offset of the first byte >= 0x80).
    Formatting picks the ASCII emitter for it by type, with no scan, in both syntaxes.
  - `%{ascii}s` is a compile-time requirement on the argument's type, not a runtime refusal, so
    formatting stays infallible at runtime.
  - Grapheme segmentation, display width and the ASCII scan live in the `eaux::text::unicode`
    package (below), which `static_format` links. Its Unicode tables are generated by a script
    into a checked-in header, pinned to one Unicode version and tested against `GraphemeBreakTest.txt`.
  - MSVC builds with `/utf-8`. See ADR 0002.
- Formatting at compile time (`constexpr` output) is out of scope.

### Build, tests, CI

- **Language and layout:**
  - C++23.
  - Everything in the C++ tree lives under the `eaux::` umbrella namespace, one sub-namespace
    per component. See `c++/docs/adr/0001-eaux-umbrella-and-text-package.md`.
  - `c++/text/ascii` is its own header-only package and CMake `INTERFACE` target:
    `include/eaux/text/ascii_string_view.hpp`, namespace `eaux::text`, with its own tests. It
    knows nothing of `static_format` and also specializes `std::formatter`, so it works with
    `std::format` on its own. It is in scope for this issue.
  - `c++/text/unicode` is its own header-only package and `INTERFACE` target, namespace
    `eaux::text::unicode`: UAX #29 grapheme segmentation, display width (the standard's wide
    ranges), the word-wise ASCII scan, the generated tables and their generator script, and the
    `GraphemeBreakTest.txt` tests. It is useful without formatting (column layout, truncating
    labels) and is in scope for this issue.
  - `static_format` is a header-only `INTERFACE` target that links the ASCII and unicode
    targets. The single
    include is `include/eaux/text/static_format/static_format.hpp`, with internals under `detail/`.
  - CMake is used with strong reservations, and Claude maintains it. `c++/Makefile` wraps it,
    and `c++` joins `LANGUAGES` in the root Makefile.
- **Tests:**
  - doctest, via a pinned `FetchContent`, for runtime tests.
  - Plain `static_assert`s for parse-level checks.
  - `tests/fail/` holds one-file compile-fail tests that pass only when stderr matches the
    expected message.
  - Stress tests cover a ~4 KB format string and ~64 arguments.
- **CI:**
  - `.github/workflows/cpp.yml` runs four jobs, path-filtered to `c++/**`: ubuntu/g++ and
    ubuntu/clang++ (newest on the runner, pinned by name), macOS/Apple clang and windows/MSVC.
  - MSVC needs `/std:c++23preview` until MSVC 14.52.
  - Only the latest compilers matter; there is no documented floor.
  - A feature is usable only if all four jobs compile it.
  - Exercise `fixed_string` on all four jobs from the first commit.
- **Benchmark:**
  - `bench/` uses Google Benchmark against `snprintf`, `std::format_to` and fmt `FMT_COMPILE`,
    with `_pfmt` and `_bfmt` writing into preallocated buffers, plus a `std::string` set.
  - Workloads: `0x%02X`, `%8.3f`, a short mixed line, and a ~30-argument table row, plus a
    variant of the table row whose labels are `ascii_string_view`, to measure the skipped scan.
  - `make bench` runs it locally, and CI only builds it.
  - It is informational: results go in the package README, including any loss to
    `FMT_COMPILE`.

## Prior art

- fmt's `FMT_COMPILE` / `_cf` literal is the closest analog.
- As far as we know, the standard has no compiled-format facility.

## Relevant files

These are in the `stdplus` repo, not this one:

- `/home/earlye/Documents/gogs/earlye/stdplus/include/stdplus/strings/formattype/`: the
  original implementation.
- `/home/earlye/Documents/gogs/earlye/stdplus/src/test/strings/test_format.cpp`: its tests.

In this repo:

- `c++/text/static_format/docs/adr/0001-printf-user-type-specifiers.md`
- `c++/text/static_format/docs/adr/0002-utf8-width-and-truncation.md`

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
- Q: How are arguments bound and what is the result? — A: Eager calls: `"x=%d"_pfmt(a, b)`
  returns `std::string`; `"..."_pfmt.to(out_iterator, a, b)` writes without allocating. `%`
  chaining is dropped, along with the lazy expression object, its dangling-pointer gap, and the
  `std::tuple` storage idea. Early's condition: dozens of arguments must work (tabular output of
  structural analysis data).
- Q (Early): Does the `fixed_string` NTTP remove typestring's ~128-char cap? — A: Yes; `N` is
  deduced from the literal. What remains are compiler budgets (constexpr steps, template depth).
  To stay clear of them, parse into a `constexpr` array of segment descriptors and expand over
  arguments with `std::index_sequence`/folds, never per-segment or per-argument recursion. Tests
  cover a ~4 KB format string and ~64 arguments so a regression to recursion breaks the build.
- Q: How strict is the printf-side type check? — A: A specifier names a type category: `d i`
  any integral except `bool`/char types; `u o x X` any integral, negatives cast to unsigned as
  printf does; `f F e E g G a A` floating; `c` char types (assumed to exclude `int`; Early may
  revisit); `s` string-like (`char const*`, `std::string`, `std::string_view`); `p` pointers.
  Length modifiers (`hh h l ll z j t L`) are accepted and ignored, so `sprintf` strings paste in
  unchanged. Anything else is a compile error naming argument index, specifier and type. The brace
  side follows `std::format`'s rules.
- Q (Early): Which printf specifier formats user-defined types? — A: Two of them. `%v` is the
  runtime route: `std::formatter<T>` (via `std::format_to(out, "{}", v)`) if specialized, else
  `operator<<(std::ostream&, T const&)`, else a compile error naming `T` and both options. `%V`
  (Early's addition) uses this library's own `static_format::formatter<T>`, whose spec is parsed
  at compile time. For both, width and `-` pad the produced text and precision truncates, as with
  `%s`; other flags are compile errors.
- Q: How does `%V` carry a custom spec, and how do user types resolve on the brace side? — A:
  An optional braced spec before the letter: `%[N$][flags][width][.precision][{spec}]V`, e.g.
  `"%-12{.3;kN}V"`. `static_format::formatter<T>` has `static constexpr Spec parse(string_view)`
  (structural `Spec`, an invalid spec fails the build) and a runtime `format(spec, out, value)`.
  On the brace side, `{:spec}` on a user type tries `static_format::formatter<T>`, then
  `std::formatter<T>`, then `operator<<` (empty spec only). See
  `c++/format/static-format/docs/adr/0001-printf-user-type-specifiers.md`.
- Q: Resolutions for the original's gaps (proposed, no objection): dangling `auto` and stream
  state carry-over are gone by construction (eager calls, no streams); `A a F` are floating;
  `C S` (deprecated wide forms) are compile errors that say so; `+ # space` follow C semantics;
  a repeated positional (`%1%`, `%1$s`) reuses the argument; an out-of-range index and mixing
  positional with sequential are compile errors.
- Q: Alignment flags in printf syntax? — A: Match the Python/`std::format` family: `^` center,
  `=` internal, `>` kept as explicit right-align, `_` dropped. This deliberately changes the
  original's meanings (`=` center, `_` internal) so `=` means the same in `_pfmt` and `_bfmt`.
- Q: Language standard, compilers and platforms? — A: C++23, not C++20. Only the latest
  compilers matter; there is no documented version floor. CI mirrors `rust.yml`'s matrix in four
  jobs: ubuntu/g++ and ubuntu/clang++ (newest on the runner, pinned by name), macOS/Apple clang,
  and windows/MSVC (CMake `CXX_STANDARD 23`; `/std:c++23preview` until VS 18.12 / MSVC 14.52 ships
  the stable `/std:c++23`, expected November 2026). A language or library feature is usable only if
  all four jobs compile it; a lacking platform means avoiding the feature, not dropping the
  platform. Early accepts many commits chasing compiler quirks in exchange for free CI coverage.
  A 2026-10-26 check found `if consteval`, `constexpr` integer `to_chars`, `std::unreachable`,
  `std::expected`, float `to_chars` with precision and class-type NTTPs available on all current
  runners. Clang doesn't bump `__cpp_nontype_template_args`, so don't gate on it. Exercise
  `fixed_string` on all four jobs from the first commit, since older MSVC reportedly had ICEs with it.
- Q: Benchmark shape? — A: Google Benchmark via pinned `FetchContent`. Contenders: `snprintf`
  into a stack buffer, `std::format_to`, fmt `format_to` with `FMT_COMPILE`, `_pfmt.to`,
  `_bfmt.to`, all into preallocated buffers, plus a `std::string`-returning set. Workloads: `0x%02X`,
  `%8.3f`, a short mixed string/int line, and a ~30-argument table row (mostly `%10.4f` plus
  labels). `make bench` runs it locally; CI only builds it on all four jobs. Informational, with no
  pass/fail bar; results go in the package README with machine and compiler, including any loss
  to `FMT_COMPILE`. The original formattype is not vendored as a baseline.
- Q: Package layout, names and output limits? — A:
  `c++/format/static-format/include/static_format/static_format.hpp` as the single include, with
  internals under `detail/`; `tests/`, `tests/fail/`, `bench/`, `docs/adr/` beside it. Namespace
  `static_format`; the literal operators live in `static_format::literals`. `.to_n(it, n,
  args...)` mirrors `std::format_to_n` and returns `{out, size}`, `size` being the untruncated
  length. No locale support: output is always "C"-locale. Formatting at compile time (`constexpr`
  output) is out of scope for now.
- Q: How do the byte limits treat UTF-8? — A: UTF-8 everywhere, both syntaxes: width counts
  display width, string precision never splits a code point, `to_n` stops at a code-point
  boundary. No single-byte opt-out, since ASCII is byte-identical in UTF-8 and gets a fast path.
  MSVC builds with `/utf-8`. See `c++/format/static-format/docs/adr/0002-utf8-width-and-truncation.md`.
- Q (Early): Does `to_n` handle a multi-byte character at the cut? — A: It drops the partial
  character, or never emits it. UTF-8 lead bytes declare their length, so either check before
  emitting or step back at most 3 continuation bytes after clamping.
- Q: Code points or grapheme clusters as the unit? — A: Graphemes, for width, precision and the
  `to_n` cut (Early: "graphemes matter"). Emitters are chosen at compile time for literals (ASCII
  vs UTF-8, with baked-in boundaries); runtime strings are scanned up front for bytes >= 0x80
  rather than decoded character by character, on Early's wager that this is usually faster.
  ADR 0002 updated.
- Q (Early): Can a braced spec on `%s` opt out of UTF-8 handling? — A: Yes, through a newtype.
  `static_format::ascii_string_view` validates at construction (`consteval` `_ascii` literal, or
  an `expected`-returning `from`) and gets the ASCII emitter by type with no scan. `%{ascii}s`
  requires that type at compile time instead of refusing at runtime, which would need an error
  channel. Braced specs are allowed on any built-in specifier, with unknown keys rejected. `ascii`
  was chosen over `utf:false` because it names what the string is. The benchmark gains an
  ASCII-newtype table-row variant, since the gain is unmeasured. ADRs 0001 and 0002 updated.
- Q (Early): Where should `ascii_string_view` live? — A: Outside `static_format`, since it is
  a vocabulary type independent of formatting. That needs an umbrella namespace for the C++ tree:
  `eaux::`. `eaux::text::ascii_string_view` gets its own header-only package `c++/text/ascii`
  (with `from`, the `_ascii` literal in `eaux::text::literals`, and a `std::formatter`
  specialization), and `eaux::static_format` depends on it, not the reverse. The ASCII fast path
  and `%{ascii}s` stay in `static_format`. Cost accepted: `static_format` is no longer a single
  copyable header. A `text` sibling such as a validated UTF-8 view can follow later.
- Q (Early): Why not `eaux::text::static_format`? — A: Agreed; formatting turns values into
  text and depends on `eaux::text`, so a separate `format` area implied a false independence.
  The directory matches the namespace: `c++/text/static_format` (underscore, at Early's request).
  The same reasoning moved grapheme segmentation, display width, the ASCII scan and the Unicode
  tables into a third package, `eaux::text::unicode` at `c++/text/unicode`, which
  `static_format` links.
