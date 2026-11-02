CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -std=c11
LDLIBS = -lm -pthread
HEADERS = server_common.h game_core.h ../overbug_world.h

all: server_single server_threads

server_single: server_single.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

server_threads: server_threads.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	rm -f server_single server_threads

.PHONY: all clean
