// expect: has no conversion specifier
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto f = "abc %"_pfmt;
