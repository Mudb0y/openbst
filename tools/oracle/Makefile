CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu11
PKGS   := unicorn

BUILD  := ../../build
BIN    := $(BUILD)/oracle
NEBIN  := $(BUILD)/neoracle
SRC    := winemu.c shims.c main.c
NESRC  := neemu.c neshims.c nemain.c
OBJ    := $(SRC:%.c=$(BUILD)/%.o)
NEOBJ  := $(NESRC:%.c=$(BUILD)/%.o)

CFLAGS += $(shell pkg-config --cflags $(PKGS))
LIBS   := $(shell pkg-config --libs $(PKGS))

all: $(BIN) $(NEBIN)

$(BUILD)/%.o: %.c winemu.h neemu.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(OBJ) $(LIBS) -o $@

$(NEBIN): $(NEOBJ)
	$(CC) $(NEOBJ) $(LIBS) -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
