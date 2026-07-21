CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?= -pthread

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:.cpp=.o)
BIN := wsrxaprs

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ)

src/%.o: src/%.cpp
	$(CXX) -Isrc $(CXXFLAGS) -pthread -c $< -o $@

clean:
	rm -f $(BIN) src/*.o
