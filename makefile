BUILD_DIR := build

.PHONY: all build test play train clean

all: build

build:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

play: build
	./$(BUILD_DIR)/zilch play

train: build
	./$(BUILD_DIR)/zilch train

clean:
	rm -rf $(BUILD_DIR)
