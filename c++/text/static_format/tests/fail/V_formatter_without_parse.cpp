// expect: formatter<T> needs a static constexpr parse
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

struct force {};
template <>
struct eaux::text::static_format::formatter<force> {
  template <class Out>
  static Out format(int, Out out, force) {
    return out;
  }
};
auto s = "%V"_pfmt(force{});
