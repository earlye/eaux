# Builds and tests every language tree. Each tree's default goal (`all`)
# builds and tests everything in it, so `make`, `make build` and `make test`
# all do the full run. Any other goal (e.g. `make clean`) is forwarded to
# every tree as-is.
LANGUAGES := go rust
.DEFAULT_GOAL := build

.PHONY: all build test $(LANGUAGES)
all build test: $(LANGUAGES)

$(LANGUAGES):
	$(MAKE) -C $@

%:
	@set -e; for lang in $(LANGUAGES); do $(MAKE) -C $$lang $@; done
