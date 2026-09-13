CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu11 -I../../include
BUILD  := ../../build
BIN    := $(BUILD)/synth
BIN2   := $(BUILD)/interptest
BIN3   := $(BUILD)/prosodytest
BIN4   := $(BUILD)/durtest
BIN5   := $(BUILD)/normtest
OBJ    := $(BUILD)/synth_core.o $(BUILD)/synth_main.o
CORE   := $(BUILD)/synth_core.o $(BUILD)/interp_core.o

all: $(BIN) $(BIN2) $(BIN3) $(BIN4) $(BIN5)

$(BUILD)/synth_core.o: ../../src/synth.c ../../include/bst_synth.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/synth_main.o: main.c ../../include/bst_synth.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/interp_core.o: ../../src/interp.c ../../include/bst_synth.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/interptest.o: interptest.c ../../include/bst_synth.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ) $(BUILD)/interp_core.o
	$(CC) $(OBJ) $(BUILD)/interp_core.o -o $@

$(BIN2): $(CORE) $(BUILD)/interptest.o
	$(CC) $(CORE) $(BUILD)/interptest.o -o $@

$(BUILD)/prosodytest.o: prosodytest.c ../../include/bst_synth.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN3): $(CORE) $(BUILD)/prosodytest.o
	$(CC) $(CORE) $(BUILD)/prosodytest.o -o $@

$(BUILD)/durtest.o: durtest.c ../../include/bst_synth.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN4): $(CORE) $(BUILD)/durtest.o
	$(CC) $(CORE) $(BUILD)/durtest.o -o $@

$(BUILD)/text_core.o: ../../src/text.c ../../include/bst_text.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/normtest.o: normtest.c ../../include/bst_text.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/dict_core.o: ../../src/dict.c ../../include/bst_text.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/lts_core.o: ../../src/lts.c ../../include/bst_text.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN5): $(BUILD)/text_core.o $(BUILD)/dict_core.o $(BUILD)/lts_core.o $(BUILD)/normtest.o
	$(CC) $(BUILD)/text_core.o $(BUILD)/dict_core.o $(BUILD)/lts_core.o $(BUILD)/normtest.o -o $@

clean:
	rm -f $(OBJ) $(CORE) $(BIN) $(BIN2) $(BUILD)/interptest.o

.PHONY: all clean
