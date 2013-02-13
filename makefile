SHELL = /bin/sh
CWD := $(shell pwd)
CFLAGS ?= -Wall -pedantic -O3 -std=gnu99
CPPFLAGS ?=
LDFLAGS ?= -lrt
SRCS := \
	$(wildcard *.c)
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
