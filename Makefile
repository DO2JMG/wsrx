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

ifeq ($(filter clean uninstall,$(MAKECMDGOALS)),)
ifndef WSRX_LIBCURL
WSRX_LIBCURL := $(shell echo 'int main(){return curl_global_init(0);}' | $(CXX) -x c++ -include curl/curl.h - -o /dev/null -lcurl >/dev/null 2>&1 && echo 1 || echo 0)
endif
ifeq ($(WSRX_LIBCURL),1)
$(info wsrx: uploads via libcurl (persistent connections))
else
$(info wsrx: uploads use the curl program (libcurl4-openssl-dev not installed or WSRX_LIBCURL=0). For persistent connections: sudo apt install libcurl4-openssl-dev, then make clean && make)
endif
endif

ifeq ($(WSRX_LIBCURL),1)
WSRX_DEFS := -DWSRX_HAVE_LIBCURL
WSRX_LIBS := -lcurl
endif

.PHONY: all clean install uninstall

all: $(TARGET) $(WEBTARGET)

$(TARGET): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(WSRX_LIBS) $(LDLIBS)

$(WEBTARGET): $(WEBOBJ)
	$(CXX) $(LDFLAGS) -o $@ $(WEBOBJ) $(LDLIBS)

src/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(WSRX_DEFS) $(CXXFLAGS) -c $< -o $@

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
