CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g
SRC_COMMON := src/checksum.c src/common.c

.PHONY: all clean

all: bin/server bin/client

bin:
	mkdir -p bin

bin/server: src/server.c $(SRC_COMMON) | bin
	$(CC) $(CFLAGS) -o $@ src/server.c $(SRC_COMMON)

bin/client: src/client.c $(SRC_COMMON) | bin
	$(CC) $(CFLAGS) -o $@ src/client.c $(SRC_COMMON)

clean:
	rm -rf bin
