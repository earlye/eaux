# eaux::text::ascii

`eaux::text::ascii_string_view`: a `std::string_view` that has been checked, once, to hold
no byte >= 0x80. Code that counts bytes as characters (fixed-width columns, protocol tokens)
can take this type instead of re-checking. Header-only, C++23, CMake target
`eaux::text::ascii`.

```cpp
#include <eaux/text/ascii_string_view.hpp>

using namespace eaux::text::literals;

constexpr auto id = "column"_ascii;  // checked at compile time; a non-ASCII literal fails the build

auto checked = eaux::text::ascii_string_view::from(input);  // std::expected<ascii_string_view, std::size_t>
if (!checked) report_non_ascii_at(checked.error());          // offset of the first byte >= 0x80

std::format("{:>8}", id);  // formats like std::string_view
```

`substr` keeps the guarantee; there is no unchecked constructor. `eaux::text::static_format`
gives the type an ASCII fast path and `%{ascii}s` requires it.
