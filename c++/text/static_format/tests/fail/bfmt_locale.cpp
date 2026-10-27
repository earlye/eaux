// expect: the L \(locale\) option is not supported
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{:L}"_bfmt(1);
