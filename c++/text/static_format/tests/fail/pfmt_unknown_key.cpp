// expect: unknown key
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto f = "%{utf8}s"_pfmt;
