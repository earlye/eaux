// expect: an argument is passed but the format string never uses it
// expect: ArgNumber = 2;|printf_argument_used<2[^0-9]
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "%d"_pfmt(1, 2);
