// expect: cannot mix positional
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto f = "%1$d %d"_pfmt;
