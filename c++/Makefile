# Builds and tests the C++ tree through CMake, once with g++ and once with clang++.
# `make bench` builds the benchmarks optimized and runs them with each compiler; BENCH_ARGS passes
# Google Benchmark flags and BENCH_PREFIX a launcher such as `taskset -c 5`.
# `make bench-record` benchmarks the committed tree with every compiler on the host and appends
# the medians to text/static_format/bench/bench.jsonl (see bench/record.py).
CMAKE ?= cmake
CTEST ?= ctest
GXX ?= g++
CLANGXX ?= clang++
BUILD ?= build
BENCH_ARGS ?=
BENCH_PREFIX ?=
BENCH_CPU ?= 5
PYTHON ?= python3
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
GENERATOR ?= $(if $(shell command -v ninja 2>/dev/null),-G Ninja)
COMPILERS := gcc clang
cxx_gcc = $(GXX)
cxx_clang = $(CLANGXX)

$(info Entering directory `$(shell pwd)') # '

.DEFAULT_GOAL := all

.PHONY: all build test bench bench-record clean $(addprefix build-,$(COMPILERS)) $(addprefix test-,$(COMPILERS)) $(addprefix bench-,$(COMPILERS))
all: test
build: $(addprefix build-,$(COMPILERS))
test: $(addprefix test-,$(COMPILERS))
bench: $(addprefix bench-,$(COMPILERS))

$(addprefix build-,$(COMPILERS)): build-%:
	$(CMAKE) -S . -B $(BUILD)/$* $(GENERATOR) -DCMAKE_CXX_COMPILER=$(cxx_$*) -DEAUX_WARNINGS_AS_ERRORS=ON
	$(CMAKE) --build $(BUILD)/$* --parallel $(JOBS)

$(addprefix test-,$(COMPILERS)): test-%: build-%
	$(CTEST) --test-dir $(BUILD)/$* --output-on-failure --parallel $(JOBS)

$(addprefix bench-,$(COMPILERS)): bench-%:
	$(CMAKE) -S . -B $(BUILD)/bench-$* $(GENERATOR) -DCMAKE_CXX_COMPILER=$(cxx_$*) -DCMAKE_BUILD_TYPE=Release \
		-DEAUX_BUILD_TESTS=OFF -DEAUX_BUILD_BENCH=ON
	$(CMAKE) --build $(BUILD)/bench-$* --parallel $(JOBS)
	$(BENCH_PREFIX) $(BUILD)/bench-$*/text/static_format/bench/eaux_text_static_format_bench $(BENCH_ARGS)

bench-record:
	$(PYTHON) text/static_format/bench/record.py --cpu $(BENCH_CPU) --cmake $(CMAKE) --build $(BUILD)/record

clean:
	rm -rf $(BUILD)
