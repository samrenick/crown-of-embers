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
APP := build/Crown of Embers.app
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
	rm -rf "/Applications/Crown of Embers.app"
	cp -R "$(APP)" /Applications/

# Regenerate assets/audio (needs FluidSynth and a GM soundfont: make audio SOUNDFONT=path/to.sf3).
build/qoaconv: tools/qoaconv.cpp | build
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

audio: build/qoaconv
	python3 tools/compose.py "$(SOUNDFONT)"

clean:
	rm -rf build

.PHONY: run clean app install audio
-include $(OBJ:.o=.d)
