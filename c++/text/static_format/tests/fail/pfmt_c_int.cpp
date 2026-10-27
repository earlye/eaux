// expect: %c needs a character type
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "%c"_pfmt(65);
