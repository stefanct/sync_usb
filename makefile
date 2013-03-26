SHELL = /bin/sh
CWD := $(shell pwd)
CFLAGS ?= -Wall -pedantic -O3 -std=gnu99 -g -Wno-unused-function
CPPFLAGS ?= -I java2arduino-c -I realtimeify
LDFLAGS ?= -lrt -lusb-1.0
SRCS := \
	$(wildcard *.c) \
	$(wildcard java2arduino-c/*.c) \
	$(wildcard realtimeify/*.c)
OBJS = $(SRCS:%.c=%.o)
DEPS = $(SRCS:%.c=%.d)
EXE := main.exe

all: $(EXE)

.PRECIOUS: $(OBJS)
%.exe: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

-include $(OBJS:.o=.d)

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@
	$(CC) -MM $(CFLAGS) $*.c > $*.d

clean:
	rm -rf $(EXE) $(OBJS) $(DEPS)

.PHONY = clean all
