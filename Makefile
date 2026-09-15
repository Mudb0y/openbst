BUILD := build

all:
	$(MAKE) -C tools/synth

# The oracle runs the original binaries to compare against. It is only needed
# to prove the library, not to use it.
oracle:
	$(MAKE) -C tools/oracle

# Writes src/data again from the binaries under dll. Only needed when a table
# map changes; what is checked in is already what this produces.
lift: all
	bash tools/lift.sh

test: all oracle
	@for t in tests/*.sh; do bash $$t; done

clean:
	$(MAKE) -C tools/synth clean
	$(MAKE) -C tools/oracle clean

.PHONY: all oracle lift test clean
