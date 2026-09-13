CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu11 -I../../include
BUILD  := ../../build
BIN    := $(BUILD)/synth
BIN2   := $(BUILD)/interptest
OBJ    := $(BUILD)/synth_core.o $(BUILD)/synth_main.o
CORE   := $(BUILD)/synth_core.o $(BUILD)/interp_core.o

all: $(BIN) $(BIN2)

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

clean:
	rm -f $(OBJ) $(CORE) $(BIN) $(BIN2) $(BUILD)/interptest.o

.PHONY: all clean
