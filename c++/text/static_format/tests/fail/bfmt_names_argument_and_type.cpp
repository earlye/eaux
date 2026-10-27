// expect: presentation type not valid for this argument
// expect: ArgId = 1|brace_builtin_argument<1
// expect: Type = 'd'; T = double|brace_builtin_argument<1[^>]*, *double,|brace_builtin_argument<1,[0-9]+,T,[^=]*T=double
#include <eaux/text/static_format/static_format.hpp>

using namespace eaux::text::static_format::literals;

auto s = "{} {:d}"_bfmt(1, 2.5);
