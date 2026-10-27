// expect: argument index above 1000000
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{:99999999999}"_bfmt(1);
