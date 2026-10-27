// expect: more arguments were passed than
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "%d"_pfmt(1, 2);
