// expect: sign, # and 0 apply only to numbers
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{:+c}"_bfmt('a');
