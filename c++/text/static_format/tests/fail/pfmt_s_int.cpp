// expect: %s needs a string
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "%s"_pfmt(42);
