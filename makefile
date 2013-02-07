SHELL = /bin/sh
CWD := $(shell pwd)
CFLAGS ?= -Wall -pedantic -O3 -std=gnu99
CPPFLAGS ?=
LDFLAGS ?= -lrt
SRCS := \
	$(wildcard *.c)
OBJS = $(SRCS:%.c=%.o)
EXE := main.exe

all: $(EXE)

.PRECIOUS: $(OBJS)
%.exe: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

clean:
	rm -rf $(EXE) $(OBJS)

.PHONY = clean all
