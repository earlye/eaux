// expect: bad_force_spec
#include <eaux/text/static_format/static_format.hpp>
#include <string_view>

using namespace eaux::text::static_format::literals;

struct force {};
void bad_force_spec();  // not constexpr: reaching it during constant evaluation fails the build
template <>
struct eaux::text::static_format::formatter<force> {
  struct spec {};
  static constexpr spec parse(std::string_view text) {
    if (!text.empty()) bad_force_spec();
    return {};
  }
  template <class Out>
  static Out format(spec, Out out, force) {
    return out;
  }
};
auto s = "%{nonsense}V"_pfmt(force{});
