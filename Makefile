.PHONY: all clean
.SILENT:

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wpedantic -Wextra
DBGFLAGS ?= -g -fsanitize=address

BUILD_DIR = build
BACKENDS = src/backends/linux_x64.c


all: main

debug: $(BUILD_DIR) src/*.c $(BACKENDS)
	$(CC) $(CFLAGS) $(DBGFLAGS) -o $(BUILD_DIR)/$@ -I include -I src src/*.c $(BACKENDS)

main: $(BUILD_DIR) src/*.c $(BACKENDS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ -I include -I src src/*.c $(BACKENDS)

tests: tests/test.c include/*.h src/tokenizer.c
	$(CC) -I include tests/test.c src/tokenizer.c -o test

out: out.S
	nasm -felf64 -o out.o out.S
	ld -o out out.o
	rm out.o

src/keywords.c: src/keywords.gperf
	gperf src/keywords.gperf --output-file=src/keywords.c

$(BUILD_DIR):
	mkdir -p build

clean:
	rm -rf build
