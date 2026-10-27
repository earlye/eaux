// expect: needs an eaux::text::ascii_string_view
#include <eaux/text/static_format/static_format.hpp>
#include <string_view>

using namespace eaux::text::static_format::literals;

auto s = "%{ascii}s"_pfmt(std::string_view("x"));
