// expect: has no closing
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto f = "%{ascii s"_pfmt;
