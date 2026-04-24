CC      = gcc
CFLAGS  = -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -Iinclude
TARGET  = nexs

SRCDIR  = src
SRCS    = $(SRCDIR)/buddy.c \
          $(SRCDIR)/utils.c \
          $(SRCDIR)/value.c \
          $(SRCDIR)/dynarray.c \
          $(SRCDIR)/registry.c \
          $(SRCDIR)/lexer.c \
          $(SRCDIR)/parser.c \
          $(SRCDIR)/eval.c \
          $(SRCDIR)/builtins.c \
          $(SRCDIR)/sysio.c \
          $(SRCDIR)/sysproc.c \
          $(SRCDIR)/main.c

OBJS    = $(SRCS:.c=.o)
HDR     = include/basereg.h include/utf8.h

.PHONY: all clean test debug run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)
	@echo "✓ Build OK → ./$(TARGET)"

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(HDR)
	$(CC) $(CFLAGS) -c -o $@ $<

debug: $(SRCS) $(HDR)
	$(CC) -O0 -g -std=c11 -Wall -Wextra -Iinclude -fsanitize=address,undefined -o $(TARGET)_dbg $(SRCS)
	@echo "✓ Debug build → ./$(TARGET)_dbg"

test: $(TARGET)
	@echo "=== Test base ==="
	@echo 'x = 42\nout x' | ./$(TARGET)
	@echo 'out 1 + 2 * 3' | ./$(TARGET)
	@echo 'fn sq(n) { ret n * n }\nout sq(7)' | ./$(TARGET)
	@echo ""
	@echo "=== Test array ==="
	@echo 'arr[0] = 10\narr[1] = 20\narr[2] = arr[0] + arr[1]\nout arr[2]' | ./$(TARGET)
	@echo ""
	@echo "=== Test stringhe ==="
	@echo 'out "hello" + " world"' | ./$(TARGET)
	@echo ""
	@echo "=== Test registro ==="
	@echo 'reg /env/ver = 1\nout reg /env/ver' | ./$(TARGET)
	@echo ""
	@echo "=== Test ls ==="
	@echo 'ls /sys' | ./$(TARGET)
	@echo ""
	@echo "=== Test script ==="
	@if [ -f test.nx ]; then ./$(TARGET) test.nx; fi
	@echo ""
	@echo "✓ Tutti i test completati"

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET)_dbg $(SRCDIR)/*.o
