# static_format: per-call compile-time inlining policy

## Context

PR #10 (static-format-float-speed) forces the per-field glue inline with
`EAUX_STATIC_FORMAT_ALWAYS_INLINE`: `to`, `render`, the segment-render lambda and `write_float`.
The float conversion is left to the compiler, which keeps it out of line. Without the forcing,
clang's inliner put the segment renderer over its cost threshold (cost 385 against 325) at
call sites with many fields, so the renderer was called out of line 16 times inside the batch
loop.

Measured trade-off of the forced inlining, at the PR #10 head:

- **Gain:** the batch of 48 floats is 28% faster under clang and 17–21% under g++. Single
  fields stay within ±3%.
- **Cost:** some short calls that return a `std::string` (`operator()`) got slower. g++'s
  `string_mixed` is about 25% slower with `_bfmt` and 6% with `_pfmt`, and clang's
  `string_row` `_bfmt` is about 10% slower.
- **Tried and rejected:** forcing inlining only on the `to(...)` path. It fixed clang's
  `string_row` and kept the batch gains, but g++'s `string_mixed` stayed about 21% slower.
  Untested guess: the forced inlining of `write_float` or of the segment lambda is what costs
  the `std::string` path.

So the right amount of inlining differs by call site: a hot loop formatting many fields wants
the glue inlined, while a program with many distinct format strings, or short `std::string`
calls, may not. Early raised whether this should be tunable.

## Design discussed (not settled)

- **Where the policy goes:** pass it as a compile-time argument per call, e.g.
  `.to<policy>(buf, ...)`, with a per-format-string default that the call can override. Early
  prefers per call.
- **Not macros or CMake options:** a header-only library whose inline functions get different
  bodies in different translation units breaks the one-definition rule silently.
- **Encoding:** a struct of two bitfields, one for "force inline" and one for "force out of
  line". A site in neither is left to the compiler. A site in both is a `static_assert` error.
- **Bits named by role, not by function:** for example `field_glue`, `literal`,
  `float_conversion`, `int_conversion`, `text_measure`. Bits are public API, and function
  names would freeze the internals.
- **Mechanism:** attributes cannot depend on a template parameter. So each site would have a
  force-inline body, a force-inline wrapper, a `noinline` wrapper and a plain wrapper, with
  `if constexpr` on the policy choosing between them. This works on GCC and Clang, and MSVC
  has `__forceinline` and `__declspec(noinline)`.
- **Invariant:** a policy never changes output. The existing output tests then run under a few
  policies (all forced inline, all forced out of line, the default, and each bit flipped) and
  must produce identical bytes.
- **Conversion backend:** leave it out of the policy. Policies cannot control `#include`s, so
  the vendored headers compile either way.

## Lessons from PR #10's inlining investigation

- **Not every conversion function is heavy:** marking `precision_chars` `noinline` made single
  fields about 14% slower. It takes the format as a runtime argument and only simplifies once
  inlined, so it behaves like glue. The genuinely heavy function, `fmt_port::format_float`, is
  already never inlined.
- **`[[gnu::flatten]]` still needs testing.** The suggestion that it is a bad fit was never
  measured. It rests on reasoning that copying the conversion into every field of a format
  string would bloat code and the instruction cache. Benchmark it as one of the policy options:
  batch, single-field and `std::string` rows under g++ and clang, plus binary size.
- **MSVC's buffer check:** with `write_float` forced inline, MSVC's static check (C4789) sees
  constant precisions, and has produced a false positive before. PR #10 commit db020ad
  side-steps it for constant precisions of 120 or more.

## Relevant files

- `c++/text/static_format/include/eaux/text/static_format/detail/sink.hpp`:
  `EAUX_STATIC_FORMAT_ALWAYS_INLINE` and `EAUX_STATIC_FORMAT_NOINLINE`.
- `c++/text/static_format/include/eaux/text/static_format/static_format.hpp`: `to`, `render`
  and the segment lambda.
- `c++/text/static_format/include/eaux/text/static_format/detail/write.hpp`: `write_float` and
  `put_padded`.
- `c++/text/static_format/README.md`: the benchmark tables and the forced-inlining trade-off
  paragraph.
- `c++/text/static_format/docs/adr/0003-vendored-float-conversion.md`
