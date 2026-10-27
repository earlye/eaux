// expect: the \? \(debug\) presentation type is not supported
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{:?}"_bfmt("x");
