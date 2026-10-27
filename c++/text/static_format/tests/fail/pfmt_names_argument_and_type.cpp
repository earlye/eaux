// expect: %d and %i need an integral argument
// expect: ArgNumber = 2|printf_argument<2
// expect: double
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "%d %d"_pfmt(1, 2.5);
