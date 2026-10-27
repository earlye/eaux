// expect: conflicting alignment flags
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto f = "%-^5d"_pfmt;
