CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

.PHONY: all test clean

all: pl0c

pl0c: compiler.cc
	$(CXX) $(CXXFLAGS) -o pl0c compiler.cc

test: pl0c
	cd test && ./test.sh

clean:
	rm -f pl0c
