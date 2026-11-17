CLANG_FORMAT ?= clang-format
CPP_FILES := $(wildcard src/*.cpp src/*.h)

# Compile the firmware
.PHONY: build
build:
	pio run

# Upload firmware to the ESP32
.PHONY: upload
upload:
	pio run -t upload

# Open the Serial Monitor (Ctrl+C to exit)
.PHONY: monitor
monitor:
	pio device monitor

# Build, upload, and immediately open the Serial Monitor
.PHONY: flash
flash:
	pio run -t upload -t monitor

# Remove build artifacts
.PHONY: clean
clean:
	pio run -t clean

# Generate compile_commands.json for VS Code / clangd autocomplete
.PHONY: compiledb
compiledb:
	pio run -t compiledb
	python3 script/fix_compiledb.py

# Format project C++ sources and headers
.PHONY: format
format:
	$(CLANG_FORMAT) -i $(CPP_FILES)

# Check formatting without modifying files
.PHONY: check
check:
	$(CLANG_FORMAT) --dry-run --Werror $(CPP_FILES)
