# `eaux::` umbrella namespace, with validated text types in their own package

All C++ in this repo lives under one umbrella namespace, `eaux::`, with one sub-namespace per
component (`eaux::static_format`, `eaux::text`). This follows the one-project-namespace convention
of boost, absl and folly, and avoids claiming global names. `eaux::text::ascii_string_view`, a
validated type proving a string has no bytes >= 0x80, is a vocabulary type rather than a
formatting detail, so it lives in its own header-only package, `c++/text/ascii`. `static_format`
depends on it to choose an ASCII emitter at compile time; the ASCII package does not depend on
`static_format`.

## Considered Options

- Keep `ascii_string_view` inside `static_format`. Rejected: code wanting the type would have to
  depend on a formatting library.
- Leave `static_format` and the ASCII type as separate top-level namespaces. Rejected as a
  collision risk with no umbrella to group the tree's components.

## Consequences

`static_format` is no longer a single self-contained header; using it means taking the ASCII
package too.
