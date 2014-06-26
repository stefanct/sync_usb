SHELL = /bin/sh
CWD := $(shell pwd)
CFLAGS ?= -std=gnu1x -Wall -Wextra -O0 -g -Wno-unused-parameter -Wno-unused-function -Wno-comment
VPATH ?= lib/ java2arduino-c/ java2arduino-c/common/ realtimeify/ ../../
override CPPFLAGS += $(patsubst %,-I%,$(subst :, ,$(VPATH)))
LDFLAGS ?= -lrt -lusb-1.0 -pthread
EXES = benchmark.exe sync.exe debug.exe
SRCS := $(wildcard $(addsuffix *.c,$(VPATH)))
OBJS = $(SRCS:%.c=%.o)
DEPS = $(SRCS:%.c=%.d) $(EXES:%.exe=%.d)

all: $(EXES)

.PRECIOUS: $(OBJS) $(EXES:%.exe=%.o)
%.exe: $(OBJS) $(EXES:%.exe=%.o)
	$(CC) $(CFLAGS) $(OBJS) $(@:.exe=.o) -o $@ $(LDFLAGS)

-include $(OBJS:.o=.d)

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@
	$(CC) -MM $(CFLAGS) $(CPPFLAGS) $*.c > $*.d

clean:
	rm -rf $(EXES) $(EXES:%.exe=%.o) $(OBJS) $(DEPS)

.PHONY = clean all
