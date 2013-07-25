
SRCS = wildcard.cpp mt19937ar.cpp mabipack.cpp main.cpp

.PHONY: all clean
all: mabiunpack
clean:
	rm -f mabiunpack

mabiunpack: $(SRCS)
	g++ -std=c++0x -Wall -Wextra -O2 $(SRCS) -lz -o mabiunpack


