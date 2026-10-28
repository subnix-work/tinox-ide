# Entry points for building, running and checking the Tinox IDE.
#   make bridge   build the C++ bridge (libtinoxkde)
#   make ide      build the IDE with the bridge linked
#   make run      build and start the IDE
#   make test     bridge tests (ctest) and Tinox tests
#   make fmt      clang-format the bridge, tinox fmt the sources
#   make check-env  verify the toolchain and dependencies
#   make install-resources  install the syntax definition and MIME types for this user

TINOX ?= $(shell command -v tinox 2>/dev/null || echo ../tinox/target/release/tinox)
BUILD_DIR := bridge/build
OUT := build/tinox-ide

.PHONY: all bridge ide run example docs test test-bridge test-asan test-tsan fuzz bench fmt clean check-env install-resources

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
	scripts/test-check-env.sh
	scripts/test-syntax.sh
	python3 scripts/check-docs.py
	ctest --test-dir $(BUILD_DIR) --output-on-failure
	$(TINOX) test
	TINOX=$(TINOX) scripts/test-example.sh

bench: bridge
	QT_QPA_PLATFORM=offscreen $(BUILD_DIR)/bin/bench_queue

# Bridge tests under AddressSanitizer + UBSan (leaks included) and ThreadSanitizer.
# The shell example (examples/shell): a window with menu, tree and editor built with the UI API
example: bridge
	$(TINOX) run examples/shell/Main.tnx

# API documentation of the Tinox side, from the doc comments, to build/api/index.html
docs:
	mkdir -p build/api
	$(TINOX) doc --out build/api/index.html

# The whole bridge suite on Qt's offscreen platform: unit tests per namespace, the ABI contract
# against the real bridge and the mock, header checks. No display needed.
test-bridge: bridge
	QT_QPA_PLATFORM=offscreen ctest --test-dir bridge/build --output-on-failure

# libFuzzer on the command parser and every command (clang; FUZZ_SECONDS=600 for the nightly run)
fuzz:
	scripts/fuzz.sh $(or $(FUZZ_SECONDS),60)

test-asan:
	cmake -S bridge -B bridge/build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTKDE_SANITIZE=address,undefined
	cmake --build bridge/build-asan
	ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=suppressions=$(CURDIR)/bridge/tests/lsan.supp:print_suppressions=0 \
	    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ctest --test-dir bridge/build-asan --output-on-failure

test-tsan:
	cmake -S bridge -B bridge/build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTKDE_SANITIZE=thread
	cmake --build bridge/build-tsan
	# `dialog` is left out: Qt starts and ends detached helper threads (QFileSystemModel, paint thread
	# pool) and this TSan runtime aborts in its thread registry when a later thread reuses an id
	# ("live_.try_emplace"). The dialog code runs on the UI thread only, like the other namespaces.
	TSAN_OPTIONS=suppressions=$(CURDIR)/bridge/tests/tsan.supp ctest --test-dir bridge/build-tsan --output-on-failure -E '^dialog$$'

fmt:
	find bridge -name '*.cpp' -o -name '*.h' | grep -v '/build/' | xargs -r clang-format -i
	find src tests -name '*.tnx' | xargs -r -n1 $(TINOX) fmt --write

clean:
	rm -rf $(BUILD_DIR) bridge/build-asan bridge/build-tsan build .tinox

check-env:
	scripts/check-env.sh

install-resources:
	scripts/install-resources.sh
