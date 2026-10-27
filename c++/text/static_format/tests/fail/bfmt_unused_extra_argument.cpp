// expect: an argument is passed but the format string never uses it
// expect: ArgId = 2;|brace_argument_used<2[^0-9]
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{} {}"_bfmt(1, 2, 3);
