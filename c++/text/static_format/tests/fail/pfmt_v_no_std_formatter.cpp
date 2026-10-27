// expect: %v needs a std::formatter<T> specialization or operator<<
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

struct opaque {};
auto s = "%v"_pfmt(opaque{});
