# `eaux::` umbrella namespace; text packages under `eaux::text`

All C++ in this repo lives under one umbrella namespace, `eaux::`, with one sub-namespace per
component. Formatting is a text concern, so the text packages share `eaux::text` and live under
`c++/text/`, each directory named like its namespace: `c++/text/static_format`
(`eaux::text::static_format`), `c++/text/ascii` and `c++/text/unicode` (`eaux::text::unicode`). This follows the one-project-namespace convention
of boost, absl and folly, and avoids claiming global names. `eaux::text::ascii_string_view`, a
validated type proving a string has no bytes >= 0x80, is a vocabulary type rather than a
formatting detail, so it lives in its own header-only package, `c++/text/ascii`. `static_format`
depends on it to choose an ASCII emitter at compile time; the ASCII package does not depend on
`static_format`. Grapheme segmentation, display width and the Unicode tables are likewise
independently useful, so they form `eaux::text::unicode`, which `static_format` also links.

## Considered Options

- Keep `ascii_string_view` inside `static_format`. Rejected: code wanting the type would have to
  depend on a formatting library.
- A separate `format` area (`c++/format/static-format`, `eaux::format`). Rejected: formatting
  depends on the text packages and produces text.
- Leave `static_format` and the ASCII type as separate top-level namespaces. Rejected as a
  collision risk with no umbrella to group the tree's components.

## Consequences

`static_format` is no longer a single self-contained header; using it means taking the ASCII
and unicode packages too.
