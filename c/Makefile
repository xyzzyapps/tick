CC ?= gcc
CFLAGS ?= -Wall -O2 -Iinclude -Isrc
LDFLAGS ?= -lm

SRCS = src/lexer.c src/ast.c src/parser.c src/object.c src/json.c src/coroutine.c src/ffi.c src/vm.c src/main.c
TARGET = tick.exe

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
