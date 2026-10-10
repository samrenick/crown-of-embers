CXX      := clang++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -MMD -MP $(shell pkg-config --cflags raylib)
LDFLAGS  := $(shell pkg-config --libs raylib) -framework Cocoa -framework IOKit -framework OpenGL -framework CoreVideo

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:src/%.cpp=build/%.o)

build/fe: $(OBJ)
	$(CXX) $(OBJ) $(LDFLAGS) -o $@

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

run: build/fe
	./build/fe

# macOS app bundle with raylib linked statically, so it runs without Homebrew.
APP := build/Fire Emblem.app
RAYLIB_A := $(shell pkg-config --variable=libdir raylib)/libraylib.a

app: $(OBJ)
	rm -rf "$(APP)"
	mkdir -p "$(APP)/Contents/MacOS" "$(APP)/Contents/Resources"
	$(CXX) $(OBJ) $(RAYLIB_A) -framework Cocoa -framework IOKit -framework OpenGL -framework CoreVideo \
		-framework CoreAudio -framework AudioToolbox -o "$(APP)/Contents/MacOS/fe"
	cp -R assets "$(APP)/Contents/Resources/"
	cp macos/Info.plist "$(APP)/Contents/"
	cp macos/icon.icns "$(APP)/Contents/Resources/"
	codesign --force --deep -s - "$(APP)"

install: app
	rm -rf "/Applications/Fire Emblem.app"
	cp -R "$(APP)" /Applications/

clean:
	rm -rf build

.PHONY: run clean app install
-include $(OBJ:.o=.d)
