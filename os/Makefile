# SplitOS Guest OS Image Build System
OUTPUT_DIR = ../android/app/src/main/assets
TARGET = $(OUTPUT_DIR)/guest_os.bin

.PHONY: all clean

all: $(TARGET)

$(TARGET): build_guest_os.py
	python build_guest_os.py

clean:
	rm -f $(TARGET)
