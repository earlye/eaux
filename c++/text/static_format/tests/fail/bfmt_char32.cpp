// expect: no formatter for this type
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{}"_bfmt(U'x');
