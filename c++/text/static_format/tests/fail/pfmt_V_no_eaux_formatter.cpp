// expect: %V needs a specialization of eaux::text::static_format::formatter
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

struct opaque {};
auto s = "%V"_pfmt(opaque{});
