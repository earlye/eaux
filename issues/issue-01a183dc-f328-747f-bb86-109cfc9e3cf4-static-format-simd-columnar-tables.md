# static_format: format columnar data (one container per column) as a table, with SIMD

## Context

This follows up the row-formatting investigation (`9e0ab57cf558`). Early's use case is data
stored as a struct of containers, one container per column:

```cpp
struct s { std::vector<double> a, b, c; };   // row i is a[i], b[i], c[i]
```

The goal is to format it as a header, one line per row, then a footer, in a single call,
using SIMD. "Containers" here means any container, not specifically `std::vector`.

Columns suit SIMD better than single rows. Every value in a column has the same spec and type,
so a batch of consecutive rows from one column is a contiguous vector load, and every lane does
the same work. In a single row the fields only line up like that when they all share a spec.

## Decisions so far

- **Output is pointer math into a preallocated buffer.** With fixed-width fields every row has
  the same length, so each cell is written to known bytes: offset `row * row_length + k`, where
  `k` is known at compile time.
- **Columns are bound explicitly**, e.g. `_bfmt(s.a, s.b, s.c)`. No reflection and no
  aggregate-decomposition trick.
- **Overflow is decided by a policy.** A field wider than its column breaks the fixed row
  length. Candidate rules:
  - report an error (return or throw), with the output contents undefined: the cells after
    the wide one may or may not be overwritten;
  - truncate each cell to its column width.
- **Rounding mode is chosen in the format spec.** The fast method multiplies by `10^precision`
  and rounds to an integer. It rounds twice, so its last digit can differ from
  `std::to_chars`/`printf`. A field should be able to ask for `to_chars`-exact output. `_pfmt`
  already has a slot for extra keys before the conversion letter (`%8.3{key}f`, ADR 0001).
  `_bfmt` has no such slot for built-in types yet, since `{:...}` follows `std::format`, so
  where the key goes in `{}` syntax is still open.
- **Row count.** Containers whose length can be found, such as `.size()`, give it implicitly. The
  format should also be able to cap the count, either as a literal in the spec (e.g.
  `{[...]4v}`) or from another argument (e.g. `{[...]{length-arg-selector}}`). These spellings
  are placeholders.
- **Columns of different lengths:** what to do is a policy choice.
- **Columns must be contiguous containers, checked at compile time.** A column argument must
  satisfy `std::ranges::contiguous_range` (plus `sized_range` when no row-count cap is given).
  Anything else fails with a `static_assert` diagnostic, so there is no scalar or gather
  fallback. Accepted: `std::vector`, `std::array`, `std::span` and C arrays. Rejected:
  `std::list`, `std::deque` and `std::vector<bool>`.
- **Header, row format and footer** need new syntax, extending the brace syntax.

## Open questions

- **Can the fast path round exactly?** Early asked whether multiplying by the next power of ten
  and rounding from that extra digit would get closer to `to_chars`. It makes mismatches rarer,
  but values near the rounding halfway point stay ambiguous.
  - Candidate exact approach (unverified): an FMA error-free product. `p = x * 10^n` and
    `e = fma(x, 10^n, -p)` give the exact product as `p + e`, which can then be rounded
    correctly.
  - Range limits: this only works while `10^n` is exactly representable (`n <= 22`) and the
    product fits the integer path.
  - Values outside that range would fall back to `to_chars`.
  - If this works, "exact" might be the default, and the spec key would only be needed for a
    faster, less exact mode.
  - It needs testing against `to_chars` before anyone relies on it.
- **Syntax:** the exact spelling of the row section, the row-count cap, the overflow and
  length-mismatch policies, and the rounding key. One candidate is to treat the header and
  footer as plain text around a repeated section, e.g. `{[}...{]}`. That leaves a question:
  how to tell scalar arguments (a title, totals) from column arguments when the header or
  footer takes values.
- **Policies:** how they are selected, in the spec or as a template or type parameter.

## Possible follow-ups

Not part of this issue.

- **Allow `std::vector<bool>` as a column** so bitfields can be formatted in tables.
  - It isn't a `contiguous_range`, so the contiguous-only rule above rejects it. It would
    need its own exception.
  - The standard gives no access to its packed words (no `data()`). A word-at-a-time path
    would therefore need library-specific access. The alternative is going through its
    iterators, which costs speed.
  - Turning packed bits into one byte per row is a well-known SIMD step (for example, AVX-512
    mask-to-byte moves).
  - `std::bitset` would raise the same questions.

## Blocked by

- `issues/issue-01a183af-bea4-7747-8222-9e0ab57cf558-static-format-simd-row-formatting.md`:
  the SIMD conversion investigation this builds on.

## Relevant files

- `c++/text/static_format/README.md`, sections "Brace syntax" and "printf syntax": the
  current spec grammars this would extend.
- `c++/text/static_format/docs/adr/0001-printf-user-type-specifiers.md`: the `%[...]{key}x`
  slot for extra keys.
- `c++/text/static_format/include/eaux/text/static_format/detail/write.hpp:199-210`: the
  current per-field float conversion.
- `c++/text/static_format/include/eaux/text/static_format/detail/parse.hpp`: the format-string
  parser that the new syntax would extend.
