CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wno-unused-function -Wno-unused-const-variable -DGL_SILENCE_DEPRECATION
BREW     := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
CXXFLAGS += -I$(BREW)/include
LDFLAGS  += -L$(BREW)/lib -lglfw \
            -framework OpenGL -framework Cocoa -framework IOKit \
            -framework CoreVideo -framework QuartzCore -framework CoreFoundation

TARGET = blackhole
SRC    = main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

shot: $(TARGET)
	./$(TARGET) --shot shot_mode1.ppm --mode 1 --frames 90
	./$(TARGET) --shot shot_mode2.ppm --mode 2 --frames 90
	./$(TARGET) --shot shot_mode3.ppm --mode 3 --frames 90

clean:
	rm -f $(TARGET) shot_*.ppm

.PHONY: all run shot clean
