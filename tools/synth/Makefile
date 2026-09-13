CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu11 -I../../include
BUILD  := ../../build
BIN    := $(BUILD)/synth
OBJ    := $(BUILD)/synth_core.o $(BUILD)/synth_main.o

all: $(BIN)

$(BUILD)/synth_core.o: ../../src/synth.c ../../include/bst_synth.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/synth_main.o: main.c ../../include/bst_synth.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
