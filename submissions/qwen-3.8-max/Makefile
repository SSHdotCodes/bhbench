CXX ?= clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
PREFIX := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
BIN = blackhole

$(BIN): main.cpp
	$(CXX) $(CXXFLAGS) -I$(PREFIX)/include main.cpp -o $(BIN) -L$(PREFIX)/lib -lglfw -framework OpenGL

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)

.PHONY: run clean
