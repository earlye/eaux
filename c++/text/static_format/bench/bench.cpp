// static_format against snprintf, std::format and fmt's compiled formats. Informational: there
// is no pass/fail bar. Each workload writes into a preallocated buffer, and a second set returns
// std::string. Inputs rotate through small tables so nothing folds to a constant.
#include <eaux/text/static_format/static_format.hpp>

#include <benchmark/benchmark.h>
#include <fmt/compile.h>
#include <fmt/format.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

using namespace eaux::text::static_format::literals;
using eaux::text::ascii_string_view;

namespace {

constexpr std::size_t table_size = 64;

struct inputs {
  std::array<unsigned, table_size> bytes{};
  std::array<int, table_size> ints{};
  std::array<double, table_size> doubles{};
  std::array<double, table_size> sci{};  // 17 significant digits, magnitudes 1e-9 to 1e9
  std::array<std::string, table_size> names;
  std::array<std::string_view, table_size> views{};
  std::array<ascii_string_view, table_size> ascii{};
};

inputs const& data() {
  static inputs const d = [] {
    inputs in;
    unsigned state = 12345;
    auto const next = [&] { return state = state * 1103515245u + 12345u; };
    for (std::size_t i = 0; i < table_size; ++i) {
      in.bytes[i] = next() >> 24;
      in.ints[i] = static_cast<int>(next() >> 20) - 2048;
      in.doubles[i] = static_cast<double>(static_cast<int>(next() >> 8)) / 4096.0 - 1000.0;
      double const mantissa = 1.0 + static_cast<double>(next()) / 4294967296.0 * 9.0;
      int const exponent = static_cast<int>(next() >> 16) % 19 - 9;
      in.sci[i] = (next() & 0x10000u ? -mantissa : mantissa) * std::pow(10.0, exponent);
      in.names[i] = "member_" + std::to_string(next() % 1000);
      in.views[i] = in.names[i];
      in.ascii[i] = *ascii_string_view::from(in.names[i]);
    }
    return in;
  }();
  return d;
}

// Rotates through the tables; the mask keeps the 28-wide row window in bounds.
#define BENCH(name, ...)                                    \
  void name(benchmark::State& state) {                      \
    auto const& d = data();                                 \
    char buffer[1024];                                      \
    std::size_t i = 0;                                      \
    for (auto _ : state) {                                  \
      std::size_t const k = i++ & 31;                       \
      auto result = (__VA_ARGS__);                          \
      benchmark::DoNotOptimize(result);                     \
      benchmark::DoNotOptimize(buffer);                     \
      (void)k;                                              \
    }                                                       \
  }                                                         \
  BENCHMARK(name)

// "0x%02X"
BENCH(hex_snprintf, std::snprintf(buffer, sizeof buffer, "0x%02X", d.bytes[k]));
BENCH(hex_std_format_to, std::format_to(buffer, "0x{:02X}", d.bytes[k]));
BENCH(hex_fmt_compile, fmt::format_to(buffer, FMT_COMPILE("0x{:02X}"), d.bytes[k]));
BENCH(hex_pfmt, "0x%02X"_pfmt.to(buffer, d.bytes[k]));
BENCH(hex_bfmt, "0x{:02X}"_bfmt.to(buffer, d.bytes[k]));

// "%8.3f"
BENCH(float_snprintf, std::snprintf(buffer, sizeof buffer, "%8.3f", d.doubles[k]));
BENCH(float_std_format_to, std::format_to(buffer, "{:8.3f}", d.doubles[k]));
BENCH(float_fmt_compile, fmt::format_to(buffer, FMT_COMPILE("{:8.3f}"), d.doubles[k]));
BENCH(float_pfmt, "%8.3f"_pfmt.to(buffer, d.doubles[k]));
BENCH(float_bfmt, "{:8.3f}"_bfmt.to(buffer, d.doubles[k]));

// Shortest round-trip form; printf has no equivalent. The sci table has 17-digit values.
BENCH(shortest_std_format_to, std::format_to(buffer, "{}", d.sci[k]));
BENCH(shortest_fmt_compile, fmt::format_to(buffer, FMT_COMPILE("{}"), d.sci[k]));
BENCH(shortest_bfmt, "{}"_bfmt.to(buffer, d.sci[k]));

// "%g", precision 6 by default.
BENCH(general_snprintf, std::snprintf(buffer, sizeof buffer, "%g", d.sci[k]));
BENCH(general_std_format_to, std::format_to(buffer, "{:g}", d.sci[k]));
BENCH(general_fmt_compile, fmt::format_to(buffer, FMT_COMPILE("{:g}"), d.sci[k]));
BENCH(general_pfmt, "%g"_pfmt.to(buffer, d.sci[k]));
BENCH(general_bfmt, "{:g}"_bfmt.to(buffer, d.sci[k]));

// "%e", precision 6 by default.
BENCH(scientific_snprintf, std::snprintf(buffer, sizeof buffer, "%e", d.sci[k]));
BENCH(scientific_std_format_to, std::format_to(buffer, "{:e}", d.sci[k]));
BENCH(scientific_fmt_compile, fmt::format_to(buffer, FMT_COMPILE("{:e}"), d.sci[k]));
BENCH(scientific_pfmt, "%e"_pfmt.to(buffer, d.sci[k]));
BENCH(scientific_bfmt, "{:e}"_bfmt.to(buffer, d.sci[k]));

// Scientific output: 16 lines of three float fields each, one line per value of the sci table.
#define BATCH(...)                                                    \
  [&] {                                                               \
    char* out = buffer;                                               \
    for (std::size_t j = 0; j < 16; ++j) {                            \
      double const x = d.sci[k + j];                                  \
      __VA_ARGS__;                                                    \
    }                                                                 \
    return out;                                                       \
  }()

BENCH(float_batch_snprintf,
      BATCH(out += std::snprintf(out, static_cast<std::size_t>(buffer + sizeof buffer - out), "%.6e %g %10.4f\n", x, x, x)));
BENCH(float_batch_std_format_to, BATCH(out = std::format_to(out, "{:.6e} {:g} {:10.4f}\n", x, x, x)));
BENCH(float_batch_fmt_compile, BATCH(out = fmt::format_to(out, FMT_COMPILE("{:.6e} {:g} {:10.4f}\n"), x, x, x)));
BENCH(float_batch_pfmt, BATCH(out = "%.6e %g %10.4f\n"_pfmt.to(out, x, x, x)));
BENCH(float_batch_bfmt, BATCH(out = "{:.6e} {:g} {:10.4f}\n"_bfmt.to(out, x, x, x)));

// A short mixed line.
BENCH(mixed_snprintf,
      std::snprintf(buffer, sizeof buffer, "%s: %d of %d done (%5.1f%%)", d.names[k].c_str(), d.ints[k], d.ints[k + 1],
                    d.doubles[k]));
BENCH(mixed_std_format_to,
      std::format_to(buffer, "{}: {} of {} done ({:5.1f}%)", d.views[k], d.ints[k], d.ints[k + 1], d.doubles[k]));
BENCH(mixed_fmt_compile, fmt::format_to(buffer, FMT_COMPILE("{}: {} of {} done ({:5.1f}%)"), d.views[k], d.ints[k],
                                        d.ints[k + 1], d.doubles[k]));
BENCH(mixed_pfmt, "%s: %d of %d done (%5.1f%%)"_pfmt.to(buffer, d.views[k], d.ints[k], d.ints[k + 1], d.doubles[k]));
BENCH(mixed_bfmt, "{}: {} of {} done ({:5.1f}%)"_bfmt.to(buffer, d.views[k], d.ints[k], d.ints[k + 1], d.doubles[k]));

// A 30-argument table row: two padded labels and 28 numbers.
#define F4 "%10.4f%10.4f%10.4f%10.4f"
#define PRINTF_ROW "%-12s%-12s" F4 F4 F4 F4 F4 F4 F4 "\n"
#define B4 "{:10.4f}{:10.4f}{:10.4f}{:10.4f}"
#define BRACE_ROW "{:<12}{:<12}" B4 B4 B4 B4 B4 B4 B4 "\n"
#define V4(n) v[n], v[n + 1], v[n + 2], v[n + 3]
#define VALUES V4(0), V4(4), V4(8), V4(12), V4(16), V4(20), V4(24)
#define PFMT(s) eaux::text::static_format::literals::operator""_pfmt<s>()
#define BFMT(s) eaux::text::static_format::literals::operator""_bfmt<s>()

BENCH(row_snprintf, [&] {
  double const* v = &d.doubles[k];
  return std::snprintf(buffer, sizeof buffer, PRINTF_ROW, d.names[k].c_str(), d.names[k + 1].c_str(), VALUES);
}());
BENCH(row_std_format_to, [&] {
  double const* v = &d.doubles[k];
  return std::format_to(buffer, BRACE_ROW, d.views[k], d.views[k + 1], VALUES);
}());
BENCH(row_fmt_compile, [&] {
  double const* v = &d.doubles[k];
  return fmt::format_to(buffer, FMT_COMPILE(BRACE_ROW), d.views[k], d.views[k + 1], VALUES);
}());
BENCH(row_pfmt, [&] {
  double const* v = &d.doubles[k];
  return PFMT(PRINTF_ROW).to(buffer, d.views[k], d.views[k + 1], VALUES);
}());
BENCH(row_bfmt, [&] {
  double const* v = &d.doubles[k];
  return BFMT(BRACE_ROW).to(buffer, d.views[k], d.views[k + 1], VALUES);
}());
// The same row with ascii_string_view labels, which skip the non-ASCII scan.
BENCH(row_ascii_pfmt, [&] {
  double const* v = &d.doubles[k];
  return PFMT(PRINTF_ROW).to(buffer, d.ascii[k], d.ascii[k + 1], VALUES);
}());
BENCH(row_ascii_bfmt, [&] {
  double const* v = &d.doubles[k];
  return BFMT(BRACE_ROW).to(buffer, d.ascii[k], d.ascii[k + 1], VALUES);
}());

// Returning std::string.
BENCH(string_hex_std_format, std::format("0x{:02X}", d.bytes[k]));
BENCH(string_hex_fmt_compile, fmt::format(FMT_COMPILE("0x{:02X}"), d.bytes[k]));
BENCH(string_hex_pfmt, "0x%02X"_pfmt(d.bytes[k]));
BENCH(string_hex_bfmt, "0x{:02X}"_bfmt(d.bytes[k]));
BENCH(string_mixed_std_format,
      std::format("{}: {} of {} done ({:5.1f}%)", d.views[k], d.ints[k], d.ints[k + 1], d.doubles[k]));
BENCH(string_mixed_fmt_compile,
      fmt::format(FMT_COMPILE("{}: {} of {} done ({:5.1f}%)"), d.views[k], d.ints[k], d.ints[k + 1], d.doubles[k]));
BENCH(string_mixed_pfmt, "%s: %d of %d done (%5.1f%%)"_pfmt(d.views[k], d.ints[k], d.ints[k + 1], d.doubles[k]));
BENCH(string_mixed_bfmt, "{}: {} of {} done ({:5.1f}%)"_bfmt(d.views[k], d.ints[k], d.ints[k + 1], d.doubles[k]));
BENCH(string_row_std_format, [&] {
  double const* v = &d.doubles[k];
  return std::format(BRACE_ROW, d.views[k], d.views[k + 1], VALUES);
}());
BENCH(string_row_fmt_compile, [&] {
  double const* v = &d.doubles[k];
  return fmt::format(FMT_COMPILE(BRACE_ROW), d.views[k], d.views[k + 1], VALUES);
}());
BENCH(string_row_pfmt, [&] {
  double const* v = &d.doubles[k];
  return PFMT(PRINTF_ROW)(d.views[k], d.views[k + 1], VALUES);
}());
BENCH(string_row_bfmt, [&] {
  double const* v = &d.doubles[k];
  return BFMT(BRACE_ROW)(d.views[k], d.views[k + 1], VALUES);
}());

}  // namespace
