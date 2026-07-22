KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_GNU_SOURCE
LDLIBS ?= -lm

obj-m += overbug_buttons.o
overbug_buttons-objs := driver.o

.PHONY: all module reader test server clean install unload

# server.c is replaced with the student's completed Lab 3 server before use.
all: module reader test

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

reader: reader.c overbug_protocol.h
	$(CC) $(CFLAGS) -o $@ reader.c

test: test.c
	$(CC) $(CFLAGS) -o $@ test.c

server: server.c overbug_protocol.h overbug_world.h
	$(CC) $(CFLAGS) -o $@ server.c $(LDLIBS)

install: module
	sudo insmod overbug_buttons.ko

unload:
	-sudo rmmod overbug_buttons

clean:
	-$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f reader test server
