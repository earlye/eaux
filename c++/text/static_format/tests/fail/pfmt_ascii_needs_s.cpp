// expect: applies only to %s
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto f = "%{ascii}d"_pfmt;
