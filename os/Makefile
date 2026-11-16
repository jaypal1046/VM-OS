# SplitOS Guest OS Image Build System
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Ikernel -Icompositor -Iapps

OUTPUT_DIR = ../android/app/src/main/assets
TARGET = $(OUTPUT_DIR)/guest_os.bin
TEST_BIN = test_guest_os.exe

GUEST_SRCS = apps/calculator.c apps/shell.c apps/apps.c compositor/compositor.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): build_guest_os.py $(GUEST_SRCS)
	python build_guest_os.py

test: test_guest_os.c $(GUEST_SRCS)
	$(CC) $(CFLAGS) $(GUEST_SRCS) test_guest_os.c -o $(TEST_BIN)
	./$(TEST_BIN)
	rm -f $(TEST_BIN)

clean:
	rm -f $(TARGET) $(TEST_BIN) *.o
