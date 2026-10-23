# app-agent: per-user single-instance resident agent.
CARGO ?= cargo

$(info Entering directory `$(shell pwd)') # '

.DEFAULT_GOAL := all

.PHONY: all
all: lint test

.PHONY: build
build:
	$(CARGO) build --all-targets

.PHONY: test
test:
	$(CARGO) test

.PHONY: lint
lint: fmt-check clippy

.PHONY: fmt
fmt:
	$(CARGO) fmt

.PHONY: fmt-check
fmt-check:
	$(CARGO) fmt --check

.PHONY: clippy
clippy:
	$(CARGO) clippy --all-targets -- -D warnings

# Type-check the Windows code from a non-Windows host
# (needs `rustup target add x86_64-pc-windows-gnu`).
.PHONY: check-windows
check-windows:
	$(CARGO) clippy --all-targets --target x86_64-pc-windows-gnu -- -D warnings

.PHONY: doc
doc:
	RUSTDOCFLAGS="-D warnings" $(CARGO) doc --no-deps

.PHONY: package
package:
	$(CARGO) publish --dry-run

.PHONY: clean
clean:
	$(CARGO) clean
