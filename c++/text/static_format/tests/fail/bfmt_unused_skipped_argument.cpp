// expect: an argument is passed but the format string never uses it
// expect: ArgId = 0;|brace_argument_used<0[^0-9]
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{1}"_bfmt(1, 2);
