// expect: %p needs a pointer
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "%p"_pfmt(1);
