CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
TARGET = wheel-emulator
SOURCES = src/main.cpp src/config.cpp src/input.cpp src/gamepad.cpp
OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

.PHONY: all clean install
