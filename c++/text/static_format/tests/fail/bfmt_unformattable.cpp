// expect: no formatter for this type
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

struct opaque {};
auto s = "{}"_bfmt(opaque{});
