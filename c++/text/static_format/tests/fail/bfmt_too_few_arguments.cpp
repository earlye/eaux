// expect: uses more arguments than were passed
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{1}"_bfmt(1);
