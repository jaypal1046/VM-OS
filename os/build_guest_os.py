#!/usr/bin/env python3
"""
SplitOS Guest Binary Image Builder
Generates a pre-compiled guest OS firmware image packed into assets/guest_os.bin
"""

import struct
import os
import sys

def build_guest_os_image(output_path):
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    
    magic = b"SPLITOS\0"
    version = 0x00010000       # 1.0.0
    entry_point = 0x80000000   # Kernel entry point
    ram_size = 16 * 1024 * 1024 # 16 MB guest RAM
    vram_offset = 0x00000000
    arch = 1                   # 1: Dual Mobile/Desktop Convergent
    flags = 0x00000007         # Bit 0: Telephony, Bit 1: Dual UI, Bit 2: Snapshot capable

    header = struct.pack(
        "<8sIIIIII",
        magic,
        version,
        entry_point,
        ram_size,
        vram_offset,
        arch,
        flags
    )

    # Initial boot payload with kernel signature, default settings, and app catalog
    boot_payload = (
        b"[SPLITOS_KERNEL_IMAGE_V1.0.0]\n"
        b"SUBSYSTEM=SPLIT_ANDROID_TELEPHONY\n"
        b"MODE_DEFAULT=MOBILE\n"
        b"MODE_CONVERGENT=DESKTOP\n"
        b"APPS=DIALER,SMS,TERMINAL,NOTES,SETTINGS\n"
        b"UART_BAUD=115200\n"
        b"FRAMEBUFFER=720x1280x32_RGBA\n"
    )

    # Pad image to a clean 64KB boundary
    total_size = len(header) + len(boot_payload)
    padding_size = (64 * 1024) - total_size
    padding = b"\x00" * padding_size

    with open(output_path, "wb") as f:
        f.write(header)
        f.write(boot_payload)
        f.write(padding)

    print(f"[OK] Generated SplitOS guest image: {output_path} ({os.path.getsize(output_path)} bytes)")

if __name__ == "__main__":
    out_dir = os.path.join(os.path.dirname(__file__), "..", "android", "app", "src", "main", "assets")
    out_file = os.path.join(out_dir, "guest_os.bin")
    build_guest_os_image(out_file)
