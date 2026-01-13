SUBMAKES:=$(shell find . -type f -name "Makefile" -mindepth 2)
SUBDIRS:=$(dir $(SUBMAKES))
SUBDIRS_TESTED:=$(addsuffix .tested,$(SUBDIRS))

.PHONY: all
all: $(SUBDIRS_TESTED)

%/.tested: %/Makefile
	$(MAKE) -C $(dir $@) test