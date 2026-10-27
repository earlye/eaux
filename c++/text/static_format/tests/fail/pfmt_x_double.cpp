// expect: %u, %o, %x and %X need an integral argument
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "%x"_pfmt(1.5);
