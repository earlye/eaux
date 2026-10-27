// expect: operator<< takes no format spec
#include <eaux/text/static_format/static_format.hpp>
#include <ostream>

using namespace eaux::text::static_format::literals;

struct tag {
  friend std::ostream& operator<<(std::ostream& os, tag) { return os; }
};
auto s = "{:>8}"_bfmt(tag{});
