# Runs the requested goal (default: all) in every crate under this tree.
SUBMAKES:=$(shell find . -mindepth 2 -type f -name Makefile -not -path "*/target/*")
SUBDIRS:=$(sort $(dir $(SUBMAKES)))
GOALS:=$(or $(MAKECMDGOALS),all)

.PHONY: $(GOALS)
$(GOALS):
	@set -e; for dir in $(SUBDIRS); do $(MAKE) -C $$dir $@; done
