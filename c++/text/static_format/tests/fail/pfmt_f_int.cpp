// expect: need a floating-point argument
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "%f"_pfmt(1);
