MODULE=$(shell grep ^module go.mod | awk '{print $$2}')
GOFILES=$(shell find . -type f -name "*.go")
SEMVER=0.0.1
VERTAG=-$(shell git rev-parse --short HEAD || echo "0000000")
GIT_ROOT:=$(shell git rev-parse --show-toplevel)
export PATH := $(PATH):$(HOME)/go/bin:$(GIT_ROOT)/scripts
PACKAGE_DIRS:=$(filter-out ./, $(sort $(dir $(GOFILES))))
DEPENDENCY_FILES:=$(patsubst %,%.dependencies,$(PACKAGE_DIRS))
SHELL=/bin/bash -euo pipefail
DECOLOR:=$(shell $(GIT_ROOT)/scripts/decolor.sh)
EXECUTABLE=bin/test

$(info Entering directory `$(shell pwd)') # '

all: $(EXECUTABLE)

.PHONY: info
info:
	echo GIT_ROOT: '$(GIT_ROOT)'
	echo PATH: '$(PATH)'

.PHONY: test
test: 
	rm -f .coverage.*
	$(MAKE) .coverage.html

.PHONY: dependencies
dependencies:
	rm -f .dependencies $(DEPENDENCY_FILES)
	$(MAKE) .dependencies

.dependencies: go.mod $(DEPENDENCY_FILES)
	echo "Getting dependencies"
	go get ${MODULE}
	go get -t ${MODULE}
	touch $@

%/.dependencies:
	go get ${MODULE}/$(dir $@)
	go get -t ${MODULE}/$(dir $@)
	touch $@

.uncover:
	go install github.com/gregoryv/uncover/cmd/uncover@b56b18c8233ef7a454eed0c045ecf19b30da36dd
	touch $@

$(EXECUTABLE): test $(GOFILES) go.mod Makefile
	GOOS=darwin && \
	go version && \
	go build -buildvcs=false -o "$(abspath $@)"
	test -f "$(abspath $@)"
	chmod +x "$(abspath $@)"

.coverage.html: .coverage.out 
	go tool cover -html=.coverage.out -o $(abspath $@)

.coverage.out: .coverage.out.tmp .uncover
	munge-coverage.sh
	$(HOME)/go/bin/uncover -min 5.0 .coverage.out | $(DECOLOR) 

.coverage.out.tmp: $(GOFILES)
	go test -v ./... -count=1 -coverprofile=$(notdir $@) | sed "s/^coverage:/coverage :/g"

.PHONY: view-coverage
view-coverage: .coverage.html
	open $(abspath $<)

.PHONY: clean
clean:
	rm -rf $(EXECUTABLE) .coverage.* .dependencies .uncover*~
