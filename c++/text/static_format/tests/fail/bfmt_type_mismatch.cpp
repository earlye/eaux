// expect: presentation type not valid for this argument
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{:d}"_bfmt("x");
