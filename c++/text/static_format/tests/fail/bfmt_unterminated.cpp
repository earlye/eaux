// expect: replacement field has no closing
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto f = "{0"_bfmt;
