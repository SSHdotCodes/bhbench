# Black hole relativistic ray tracer — macOS / Apple Silicon
CXX      := clang++
CXXFLAGS := -O3 -std=c++17 -Wall -Wextra -Wno-unused-parameter -DGL_SILENCE_DEPRECATION \
            -I/opt/homebrew/include -DSHADER_DIR=\"$(CURDIR)/shaders\"
LDFLAGS  := -L/opt/homebrew/lib -lglfw -lz \
            -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo

SRC := src/main.cpp
BIN := blackhole

all: $(BIN)

$(BIN): $(SRC) $(wildcard src/*.h) $(wildcard shaders/*)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

run: $(BIN)
	./$(BIN)

# macOS app bundle: `open` launches it inside the GUI session so the window
# stays open independently of the terminal that built it.
app: $(BIN)
	mkdir -p BlackHole.app/Contents/MacOS
	cp $(BIN) BlackHole.app/Contents/MacOS/$(BIN)
	cp Info.plist BlackHole.app/Contents/Info.plist

launch: app
	open BlackHole.app

test: $(BIN)
	./$(BIN) --selftest

clean:
	rm -f $(BIN)

.PHONY: all run test clean app launch
