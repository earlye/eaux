# Width, precision and `to_n` are UTF-8 aware in both syntaxes

All text is treated as UTF-8. Field width counts display width (by `std::format`'s rules),
string precision counts display width without splitting a code point, and `to_n` stops at the
last complete code point that fits while still returning the untruncated size. This applies to
`_pfmt` too, deliberately departing from C `printf`'s byte counting, so that tabular output with
labels like `µ` or `σ_max` stays column-aligned. ASCII-only text behaves exactly like byte
counting, so there is no opt-out mode; an all-bytes-below-0x80 fast path skips decoding. The build
assumes UTF-8 source and literal encoding (`/utf-8` on MSVC).

## Considered Options

- Byte counting in `_pfmt` for exact `sprintf` fidelity, Unicode awareness only in `_bfmt`.
  Rejected: columns misalign as soon as a label leaves ASCII.
- A switch for a single-byte mode. Rejected as redundant with the ASCII fast path.
