// expect: %v needs a std::formatter<T> specialization or operator<<
#include <eaux/text/static_format/static_format.hpp>
#include <format>
#include <string_view>

using namespace eaux::text::static_format::literals;

// format() takes std::format_context& rather than any context, so the type is not
// std::formattable (libc++ refuses to format it) and %v must say so up front.
struct point {};
template <>
struct std::formatter<point> : std::formatter<std::string_view> {
  auto format(point const&, std::format_context& ctx) const {
    return std::formatter<std::string_view>::format("point", ctx);
  }
};
auto s = "%v"_pfmt(point{});
