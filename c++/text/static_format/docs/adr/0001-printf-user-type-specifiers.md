# printf syntax formats user-defined types with `%v` and `%{spec}V`

`_pfmt` extends printf grammar with two specifiers for user-defined types instead of letting `%s`
accept anything formattable. `%v` is the runtime route: `std::formatter<T>` if specialized, else
`operator<<`. `%V` uses `eaux::text::static_format::formatter<T>`, whose `constexpr parse` turns an optional
braced spec placed before the letter (`%[N$][flags][width][.precision][{spec}]V`) into a
structural value at compile time. The brace is placed before the letter because `{` is not a
printf flag or digit, while a spec after the letter (`%V{...}`) would be ambiguous with literal
text. The extension leaves standard printf strings untouched.

## Considered Options

- `%s` accepts any formattable type, as boost.format does. Rejected because it throws away most
  of the compile-time type check.
- `%O` or `%U`. Rejected because both carry old BSD meanings (long octal, long unsigned).
- A spec after the letter, `%V{spec}`. Rejected as ambiguous with literal text.

## Consequences

`_bfmt` reaches the same formatters without an explicit choice. For `{:spec}` on a user type it
tries `eaux::text::static_format::formatter<T>`, then `std::formatter<T>`, then `operator<<` (empty spec
only). In printf syntax the letter makes that choice, so the asymmetry is deliberate.

The braced spec is not limited to `%V`: any built-in specifier may take one, and unknown keys are
compile errors. The first key is `ascii` on `%s` (`%{ascii}s`), which requires the argument's type
to be `eaux::text::ascii_string_view`. See ADR 0002.
