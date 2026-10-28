include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.10.0.tar.gz
  URL_HASH SHA256=acc5c7c0fbed855c4aaa2d7c3c36fb82b0c15c28f4e151e3f679c2fc0aa404af
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SYSTEM)
# fmt is fetched by the parent CMakeLists.txt.
FetchContent_MakeAvailable(benchmark)

add_executable(eaux_text_static_format_bench bench.cpp)
target_link_libraries(eaux_text_static_format_bench PRIVATE
  eaux::text::static_format benchmark::benchmark_main fmt::fmt)
eaux_target_warnings(eaux_text_static_format_bench)
