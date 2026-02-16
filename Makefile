# Simple placeholder that forwards all make commands to the go subdirectory.
# Obviously when we add other languages, this will need to be updated.
.DEFAULT_GOAL := build
%:
	$(MAKE) -C go $(MAKECMDGOALS)