CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu11
PKGS   := unicorn

BUILD  := ../../build
BIN    := $(BUILD)/oracle
SRC    := winemu.c shims.c main.c
OBJ    := $(SRC:%.c=$(BUILD)/%.o)

CFLAGS += $(shell pkg-config --cflags $(PKGS))
LIBS   := $(shell pkg-config --libs $(PKGS))

all: $(BIN)

$(BUILD)/%.o: %.c winemu.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(OBJ) $(LIBS) -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
