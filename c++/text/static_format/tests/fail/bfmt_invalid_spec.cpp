// expect: invalid format spec for a built-in type
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{:Q}"_bfmt(1);
