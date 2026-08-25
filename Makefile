CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -O2 -Iinclude
LDFLAGS := -lm
BUILD   := build
BIN     := $(BUILD)/vcpu_sim
SRC     := $(wildcard src/*.c)
OBJ     := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))

.PHONY: all run clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/%.o: src/%.c include/vcpu.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(BIN)
	./$(BIN) examples/saxpy.vasm

clean:
	rm -rf $(BUILD)
