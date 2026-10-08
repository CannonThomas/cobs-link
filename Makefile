CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Wconversion -Werror
SAN     ?= -fsanitize=address,undefined -fno-sanitize-recover=all -g
INC     := -Iinclude
SRC     := src/cobs_link.c
BUILD   := build

.PHONY: all test cross js example diagram clean

all: test cross

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_cobs_link: tests/test_cobs_link.c $(SRC) include/cobs_link.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $(INC) tests/test_cobs_link.c $(SRC) -o $@

$(BUILD)/cl_cli: tools/cl_cli.c $(SRC) include/cobs_link.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $(INC) tools/cl_cli.c $(SRC) -o $@

$(BUILD)/noisy_channel: examples/noisy_channel.c $(SRC) include/cobs_link.h | $(BUILD)
	$(CC) $(CFLAGS) $(INC) examples/noisy_channel.c $(SRC) -o $@

# unit tests, built with ASan + UBSan
test: $(BUILD)/test_cobs_link
	./$(BUILD)/test_cobs_link

# C implementation vs. independent Python implementation
cross: $(BUILD)/cl_cli
	python3 tests/cross_check.py

# JavaScript port used by the interactive demo vs. the Python implementation
js:
	python3 tests/js_vectors.py | node tests/js_check.cjs

# regenerate the README diagram from the real encoder
diagram:
	python3 tools/frame_svg.py

example: $(BUILD)/noisy_channel
	./$(BUILD)/noisy_channel

clean:
	rm -rf $(BUILD)
