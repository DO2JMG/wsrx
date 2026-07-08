CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
CPPFLAGS ?= -Isrc
LDFLAGS ?=
LDLIBS ?= -pthread

TARGET := wsrx
WEBTARGET := wsrx-web
SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:.cpp=.o)
WEBSRC := websrc/wsrx_web.cpp
WEBOBJ := $(WEBSRC:.cpp=.o)

.PHONY: all clean install uninstall

all: $(TARGET) $(WEBTARGET)

$(TARGET): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(WEBTARGET): $(WEBOBJ)
	$(CXX) $(LDFLAGS) -o $@ $(WEBOBJ) $(LDLIBS)

src/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

websrc/%.o: websrc/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(WEBOBJ) $(TARGET) $(WEBTARGET)

install: $(TARGET) $(WEBTARGET)
	install -Dm755 $(TARGET) /usr/local/bin/$(TARGET)
	install -Dm755 $(WEBTARGET) /usr/local/bin/$(WEBTARGET)
	install -Dm644 config.ini /usr/local/bin/config.ini

uninstall:
	rm -f /usr/local/bin/$(TARGET) /usr/local/bin/$(WEBTARGET) /usr/local/bin/config.ini
