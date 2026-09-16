CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu11
PKGS   := unicorn

BUILD  := ../../build
BIN    := $(BUILD)/oracle
NEBIN  := $(BUILD)/neoracle
KNGBIN := $(BUILD)/kngoracle
SRC    := winemu.c shims.c main.c
NESRC  := neemu.c neshims.c nemain.c
KNGSRC := neemu.c neshims.c kngmain.c
OBJ    := $(SRC:%.c=$(BUILD)/%.o)
NEOBJ  := $(NESRC:%.c=$(BUILD)/%.o)
KNGOBJ := $(KNGSRC:%.c=$(BUILD)/%.o)

CFLAGS += $(shell pkg-config --cflags $(PKGS))
LIBS   := $(shell pkg-config --libs $(PKGS))

all: $(BIN) $(NEBIN) $(KNGBIN)

$(BUILD)/%.o: %.c winemu.h neemu.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(OBJ) $(LIBS) -o $@

$(NEBIN): $(NEOBJ)
	$(CC) $(NEOBJ) $(LIBS) -o $@

$(KNGBIN): $(KNGOBJ)
	$(CC) $(KNGOBJ) $(LIBS) -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
