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

# The one test that needs no original binary: the library against the answers
# they gave, carried in tests/golden.txt.
selftest: all
	bash tests/selftest.sh

# Writes those answers again. Needs the binaries; the test does not.
golden: all
	bash tools/golden.sh

# Every test script, by its exit status rather than by what it prints: some
# of them report a line per language and only fail on one of them.
test: all oracle
	@fail=0; for t in tests/*.sh; do \
	    if bash $$t; then :; else fail=$$((fail+1)); echo "FAILED: $$t"; fi; \
	done; \
	if [ $$fail -eq 0 ]; then echo "all tests passed"; \
	else echo "$$fail test scripts failed"; exit 1; fi

PREFIX ?= /usr/local
DESTDIR ?=

# The library, its header and the command. Nothing else: the test drivers and
# the analysis tools stay in the tree they were built in.
install: all
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/bin
	install -m 644 $(BUILD)/libbst.a $(DESTDIR)$(PREFIX)/lib/
	install -m 755 $(BUILD)/libbst.so.0 $(DESTDIR)$(PREFIX)/lib/
	ln -sf libbst.so.0 $(DESTDIR)$(PREFIX)/lib/libbst.so
	install -m 644 include/bst.h $(DESTDIR)$(PREFIX)/include/
	install -m 755 $(BUILD)/bstspeak $(DESTDIR)$(PREFIX)/bin/

clean:
	$(MAKE) -C tools/synth clean
	$(MAKE) -C tools/oracle clean

.PHONY: all oracle lift selftest golden test install clean
