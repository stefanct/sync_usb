SHELL = /bin/sh
CWD := $(shell pwd)
CFLAGS ?= -Wall -pedantic -O3 -std=gnu99
CPPFLAGS ?=
LDFLAGS ?= -lrt
SRCS := \
	$(wildcard *.c)
OBJS = $(SRCS:%.c=%.o)

all: main

.PRECIOUS : $(OBJS)
%.exe: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@
