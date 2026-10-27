// expect: %d and %i need an integral argument
#include <eaux/text/static_format/static_format.hpp>
#include <string>

using namespace eaux::text::static_format::literals;

auto s = "%d"_pfmt(std::string("x"));
