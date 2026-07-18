KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
LDLIBS ?= -lm

obj-m += overbug_buttons.o
overbug_buttons-objs := driver.o

.PHONY: all module userspace clean install unload

all: module userspace

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

userspace: reader server

reader: reader.c overbug_protocol.h
	$(CC) $(CFLAGS) -o $@ $<

server: server.c overbug_protocol.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

install: module
	sudo insmod overbug_buttons.ko

unload:
	-sudo rmmod overbug_buttons

clean:
	-$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f reader server
