// expect: width or precision from an argument
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto f = "%*d"_pfmt;
