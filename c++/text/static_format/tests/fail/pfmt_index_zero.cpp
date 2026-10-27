// expect: positional arguments count from 1
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto f = "%0$d"_pfmt;
