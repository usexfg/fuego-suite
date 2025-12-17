all: all-release

cmake-debug:
	mkdir -p build/debug
	cd build/debug && cmake -D CMAKE_BUILD_TYPE=Debug ../..

build-debug: cmake-debug
	cd build/debug && $(MAKE)

test-debug: build-debug
	cd build/debug && $(MAKE) test

all-debug: build-debug

cmake-release:
	mkdir -p build/release
	cd build/release && cmake ../..

build-release: cmake-release
	cd build/release && $(MAKE)

build-static:
	mkdir -p build/static
	cd build/static && cmake ../..
	cd build/static && $(MAKE) SHARED=0 CC='gcc -static'

test-release: build-release
	cd build/release && $(MAKE) test

all-release: build-release build-tui

# Build TUI separately if Go is available
build-tui:
	@if command -v go >/dev/null 2>&1; then \
		echo "Building Fuego TUI"; \
		cd tui && go mod tidy && go build -o fuego-tui; \
		if [ -f "tui/fuego-tui" ]; then \
			mkdir -p build/release/src; \
			cp tui/fuego-tui build/release/src/; \
			chmod +x build/release/src/fuego-tui; \
			echo "Fuego TUI built successfully"; \
		else \
			echo "Failed to build fuego-tui binary"; \
			exit 1; \
		fi; \
	else \
		echo "Go is not installed. Skipping TUI build."; \
	fi

test-dynamic-supply:
	g++ -std=c++17 -o simple_dynamic_supply_test simple_dynamic_supply_test.cpp
	./simple_dynamic_supply_test

build-with-tests:
	mkdir -p build/test
	cd build/test && cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DCMAKE_POLICY_DEFAULT_CMP0167=OLD ../..
	cd build/test && $(MAKE)

clean:
	rm -rf build
	rm -f simple_dynamic_supply_test
	rm -f tui/build/fuego-tui

tags:
	ctags -R --sort=1 --c++-kinds=+p --fields=+iaS --extra=+q --language-force=C++ src contrib tests/gtest

.PHONY: all cmake-debug build-debug test-debug all-debug cmake-release build-release test-release all-release build-tui clean tags
