SHELL = /bin/sh

.SUFFIXES:
.SUFFIXES: .c .o

# CPPFLAGS += -DDEBUG_TRACE_EXECUTION
# CPPFLAGS += -DDEBUG_PRINT_CODE
# CPPFLAGS += -DDEBUG_STRESS_GC
# CPPFLAGS += -DDEBUG_LOG_GC
# CFLAGS += -Og -g

# CPPFLAGS += -DNAN_BOXING
CFLAGS += -Wall -Wextra -Wpedantic -Wno-unused-parameter
CFLAGS += -O3

SRCS = chunk.c debug.c vm.c memory.c value.c compiler.c scanner.c object.c table.c
OBJS = $(SRCS:.c=.o)

.PHONY: all
all: lox

lox: $(OBJS)

lox.js: $(OBJS)
	$(CC) -o $@ $(OBJS) \
		-sEXPORTED_RUNTIME_METHODS=ccall,cwrap

.PHONY: clean
clean:
	rm -f *.o lox lox.wasm lox.js
