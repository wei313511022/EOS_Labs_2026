CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
# Add only the architecture-specific link flags your design needs.
# For example, a POSIX-thread solution needs: -pthread
LDLIBS ?= -lm

TARGET := server

.PHONY: all clean

all: $(TARGET)

$(TARGET): server.c overbug_protocol.h overbug_world.h
	$(CC) $(CFLAGS) -o $@ server.c $(LDLIBS)

clean:
	rm -f $(TARGET)
