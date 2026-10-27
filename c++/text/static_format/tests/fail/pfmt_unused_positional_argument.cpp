// expect: an argument is passed but the format string never uses it
// expect: ArgNumber = 1;|printf_argument_used<1[^0-9]
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "%2$s"_pfmt("a", "b");
