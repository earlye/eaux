// expect: ascii_literal_contains_a_byte_ge_0x80
#include <eaux/text/ascii_string_view.hpp>

using namespace eaux::text::literals;

constexpr auto label = "caf\xc3\xa9"_ascii;
