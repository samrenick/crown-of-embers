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

clean:
	rm -rf build

.PHONY: run clean
-include $(OBJ:.o=.d)
