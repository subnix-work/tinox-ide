# Entry points for building, running and checking the Tinox IDE.
#   make bridge   build the C++ bridge (libtinoxkde)
#   make ide      build the IDE with the bridge linked
#   make run      build and start the IDE
#   make test     bridge tests (ctest) and Tinox tests
#   make fmt      clang-format the bridge, tinox fmt the sources
#   make check-env  verify the toolchain and dependencies

TINOX ?= $(shell command -v tinox 2>/dev/null || echo ../tinox/target/release/tinox)
BUILD_DIR := bridge/build
OUT := build/tinox-ide

.PHONY: all bridge ide run test fmt clean check-env

all: ide

bridge:
	cmake -S bridge -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build $(BUILD_DIR)

ide: bridge
	mkdir -p build
	$(TINOX) build -o $(OUT)

run: ide
	./$(OUT)

test: bridge
	ctest --test-dir $(BUILD_DIR) --output-on-failure
	$(TINOX) test

fmt:
	find bridge -name '*.cpp' -o -name '*.h' | grep -v '/build/' | xargs -r clang-format -i
	find src tests -name '*.tnx' | xargs -r -n1 $(TINOX) fmt --write

clean:
	rm -rf $(BUILD_DIR) build .tinox

check-env:
	scripts/check-env.sh
