// expect: precision applies only to floating-point and string
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{:.2}"_bfmt(1);
