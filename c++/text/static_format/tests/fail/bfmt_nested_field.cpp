// expect: nested replacement fields
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto f = "{:{}}"_bfmt;
