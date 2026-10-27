cmake_minimum_required(VERSION 3.25)
project(eaux LANGUAGES CXX)

# The libraries are header-only INTERFACE targets; this build exists for their tests and
# benchmarks.
option(EAUX_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(EAUX_BUILD_BENCH "Build the benchmarks" OFF)
option(EAUX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in tests and benchmarks" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(eaux)

if(EAUX_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
    URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SYSTEM)
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)
endif()

add_subdirectory(text/ascii)
add_subdirectory(text/unicode)
