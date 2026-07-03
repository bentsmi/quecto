.PHONY: all main debug test smoke dist-clean clean
.SILENT:

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wpedantic -Wextra
DBGFLAGS ?= -g -fsanitize=address

SOURCE = $(filter-out src/keywords.c, $(wildcard src/*.c)) src/keywords.c
BACKENDS = src/backends/linux_x64.c

BUILD_DIR = build

ERROR_EXAMPLES = examples/branch.q examples/loop.q
EXAMPLES = $(filter-out $(ERROR_EXAMPLES), $(wildcard examples/*.q))

all: main debug test


main debug: %: $(BUILD_DIR) $(BUILD_DIR)/%
	@echo "Built $@"
test: $(BUILD_DIR) $(BUILD_DIR)/test
	@echo "Built $@"
	$(BUILD_DIR)/test
	
$(BUILD_DIR)/debug: $(SOURCE) $(BACKENDS)
	$(CC) $(CFLAGS) $(DBGFLAGS) -o $@ -Iinclude $^

$(BUILD_DIR)/main: $(SOURCE) $(BACKENDS)
	$(CC) $(CFLAGS) -o $@ -Iinclude $^

$(BUILD_DIR)/test: tests/test.c src/tokenizer.c src/keywords.c src/common.c src/error.c
	$(CC) $(CFLAGS) $(DBGFLAGS) -I include $^ -o $@

src/keywords.c: src/keywords.gperf
	gperf src/keywords.gperf --output-file=src/keywords.c


smoke: main
	@echo "==BUILDING EXPECTED SUCCESS EXAMPLES=="
	@for example in $(EXAMPLES); do \
		echo "Building $$example"; \
		$(BUILD_DIR)/main build $$example && \
			echo "SUCCESS: $$example built successfully" || \
			{ echo "ERROR: $$example unexpectedly failed"; exit 1; } \
	done
	@echo "\n\n==BUILDING EXPECTED ERROR EXAMPLES=="
	@for example in $(ERROR_EXAMPLES); do \
		echo "Building $$example"; \
		($(BUILD_DIR)/main build $$example) && \
			{ echo "ERROR: $$example unexpectedly succeeded"; exit 1; } || \
			echo "SUCCESS: $$example failed successfully"; \
	done

DETECTED_OS := $(shell uname -s)

ifeq ($(DETECTED_OS),Darwin)
    NASM_FLAGS := -f macho64
    LD_FLAGS := -macos_version_min 14.1 \
    		-L/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib \
    		-lSystem -arch x86_64 -syslibroot $(shell xcrun --sdk macosx --show-sdk-path)
else ifeq ($(DETECTED_OS),Linux)
    NASM_FLAGS := -f elf64
    LD_FLAGS :=
endif

run-%: main examples/%.q
	$(BUILD_DIR)/main build examples/$*.q
	nasm $(NASM_FLAGS) -o $*.o out.S
	ld $(LD_FLAGS) -o $(BUILD_DIR)/$* $*.o
	rm -f $*.o out.S
	-@$(BUILD_DIR)/$*; echo $$?;

$(BUILD_DIR):
	mkdir -p build

dist-clean: clean
	rm -f src/keywords.c

clean:
	rm -rf build
