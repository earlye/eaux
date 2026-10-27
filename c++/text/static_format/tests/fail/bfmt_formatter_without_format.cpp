// expect: formatter<T> needs a static format\(spec, out, value\)
#include <eaux/text/static_format/static_format.hpp>
#include <string_view>

using namespace eaux::text::static_format::literals;

struct force {};
template <>
struct eaux::text::static_format::formatter<force> {
  struct spec {};
  static constexpr spec parse(std::string_view) { return {}; }
};
auto s = "{}"_bfmt(force{});
