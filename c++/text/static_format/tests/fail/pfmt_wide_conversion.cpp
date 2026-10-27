// expect: deprecated wide-character forms
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto f = "%S"_pfmt;
