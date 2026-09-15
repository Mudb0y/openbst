CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu11 -I../../include
BUILD  := ../../build
SRC    := ../../src
INC    := ../../include

# Every test driver is one .c here linked against the library modules it
# needs; the lists below are the only per-binary detail.
SYNTH   := synth interp
TEXT    := text dict lts cursors wordstream stress assemble tokenise except numbers
CORE    := synth interp text dict lts cursors phrules pairs

synth_MODS      := synth interp
interptest_MODS := synth interp
prosodytest_MODS:= synth interp
durtest_MODS    := synth interp
normtest_MODS   := text dict lts cursors wordstream
curtest_MODS    := text cursors
phtest_MODS     := text cursors phrules
pairtest_MODS   := text cursors pairs diphone
acctest_MODS    := text cursors accents
contest_MODS    := text cursors contour
frametest_MODS  := text cursors diphone frames interp synth
pipetest_MODS   := text cursors accents pairs contour diphone frames interp synth
stresstest_MODS := text cursors stress
asmtest_MODS    := text cursors stress assemble
fulltest_MODS   := text cursors stress assemble phrules accents pairs contour diphone frames interp synth
toktest_MODS    := text cursors tokenise translit except numbers wordstream dict lts
saytest_MODS    := text cursors tokenise translit except numbers wordstream dict lts stress assemble phrules accents pairs contour diphone frames interp synth
say_MODS        := text dict lts cursors wordstream
nedump_MODS     := text

BINS := synth interptest prosodytest durtest normtest curtest phtest pairtest acctest contest frametest pipetest stresstest asmtest fulltest toktest saytest say nedump

# The library proper: every module, the front end over them, and the tables
# tools/lift wrote. A caller needs libbst and include/bst.h, nothing else.
LIBMODS := text dict lts cursors wordstream stress assemble tokenise translit            except numbers phrules accents pairs contour diphone frames interp            synth speak
DATA    := $(wildcard $(SRC)/data/*.c)
DATAOBJ := $(patsubst $(SRC)/data/%.c,$(BUILD)/data_%.o,$(DATA))
LIBOBJ  := $(addprefix $(BUILD)/lib_,$(addsuffix .o,$(LIBMODS))) $(DATAOBJ)

all: $(addprefix $(BUILD)/,$(BINS)) $(BUILD)/libbst.a $(BUILD)/libbst.so $(BUILD)/bstspeak $(BUILD)/lift $(BUILD)/lifttest

$(BUILD)/data_%.o: $(SRC)/data/%.c $(INC)/bst_text.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(BUILD)/libbst.a: $(LIBOBJ)
	$(AR) rcs $@ $^

$(BUILD)/libbst.so: $(LIBOBJ)
	$(CC) -shared -o $@ $^

$(BUILD)/bstspeak: $(BUILD)/drv_bstspeak.o $(LIBOBJ)
	$(CC) $^ -o $@

$(BUILD)/lift: $(BUILD)/drv_lift.o $(LIBOBJ)
	$(CC) $^ -o $@

$(BUILD)/lifttest: $(BUILD)/drv_lifttest.o $(LIBOBJ)
	$(CC) $^ -o $@

$(BUILD)/lib_%.o: $(SRC)/%.c $(INC)/bst_synth.h $(INC)/bst_text.h $(INC)/bst_frames.h $(INC)/bst_token.h $(INC)/bst.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(BUILD)/drv_%.o: %.c $(INC)/bst_synth.h $(INC)/bst_text.h $(INC)/bst_frames.h $(INC)/bst_token.h $(INC)/bst.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# The synthesizer driver is main.c rather than synth.c, which would collide
# with the library module of that name.
$(BUILD)/synth: $(BUILD)/drv_main.o $(addprefix $(BUILD)/lib_,$(addsuffix .o,$(synth_MODS)))
	$(CC) $^ -o $@

define BINRULE
$(BUILD)/$(1): $(BUILD)/drv_$(1).o $(addprefix $(BUILD)/lib_,$(addsuffix .o,$($(1)_MODS)))
	$$(CC) $$^ -o $$@
endef
$(foreach b,$(filter-out synth,$(BINS)),$(eval $(call BINRULE,$(b))))

clean:
	rm -f $(BUILD)/lib_*.o $(BUILD)/drv_*.o $(BUILD)/data_*.o \
	      $(addprefix $(BUILD)/,$(BINS)) \
	      $(BUILD)/libbst.a $(BUILD)/libbst.so $(BUILD)/bstspeak $(BUILD)/lift

.PHONY: all clean
