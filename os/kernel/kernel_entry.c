#include "kernel_entry.h"
#include <string.h>
#include <stdio.h>

static kernel_context_t g_kcontext;

void kernel_init(void) {
    memset(&g_kcontext, 0, sizeof(g_kcontext));
    g_kcontext.width = 720;
    g_kcontext.height = 1280;
    g_kcontext.mode = 0; // Mobile mode default
    g_kcontext.vram = (uint32_t*)SPLITOS_MMIO_VRAM_BASE;
    g_kcontext.uptime_ticks = 0;

    // Send initial handshake across virtual UART to Android Host
    kernel_write_uart_string("NOTIF:SplitOS:Guest Kernel Boot Complete\n");
}

void kernel_write_uart_string(const char* str) {
    if (!str) return;
    volatile uint32_t* uart_data_reg = (volatile uint32_t*)(SPLITOS_MMIO_UART_BASE + REG_UART_DATA);
    while (*str) {
        *uart_data_reg = (uint32_t)(*str);
        str++;
    }
}

int kernel_read_uart_line(char* out_buf, size_t max_len) {
    if (!out_buf || max_len == 0) return 0;
    volatile uint32_t* uart_status_reg = (volatile uint32_t*)(SPLITOS_MMIO_UART_BASE + REG_UART_STATUS);
    volatile uint32_t* uart_data_reg = (volatile uint32_t*)(SPLITOS_MMIO_UART_BASE + REG_UART_DATA);

    size_t count = 0;
    while ((*uart_status_reg & 0x01) && count < max_len - 1) {
        char c = (char)(*uart_data_reg & 0xFF);
        if (c == '\n' || c == '\r') {
            break;
        }
        out_buf[count++] = c;
    }
    out_buf[count] = '\0';
    return (int)count;
}

void kernel_step(void) {
    g_kcontext.uptime_ticks++;

    // Check incoming UART events from host
    char line[128];
    if (kernel_read_uart_line(line, sizeof(line)) > 0) {
        if (strncmp(line, "EVENT:BATTERY:", 14) == 0) {
            // Update battery registers
        }
    }
}
