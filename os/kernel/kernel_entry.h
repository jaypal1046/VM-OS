#ifndef SPLITOS_KERNEL_ENTRY_H
#define SPLITOS_KERNEL_ENTRY_H

#include <stdint.h>
#include <stddef.h>

// Hardware MMIO Base Addresses for SplitOS Micro-Kernel
#define SPLITOS_MMIO_BASE        0x80000000U
#define SPLITOS_MMIO_VRAM_BASE   0x80000000U
#define SPLITOS_MMIO_UART_BASE   0x80100000U
#define SPLITOS_MMIO_INPUT_BASE  0x80200000U
#define SPLITOS_MMIO_SYSTEM_BASE 0x80300000U

// UART Registers
#define REG_UART_DATA     0x00U
#define REG_UART_STATUS   0x04U
#define REG_UART_CTRL     0x08U

// Display Registers
#define REG_DISP_WIDTH    0x00U
#define REG_DISP_HEIGHT   0x04U
#define REG_DISP_STRIDE   0x08U
#define REG_DISP_MODE     0x0CU // 0: Mobile, 1: Desktop

// Input Registers
#define REG_INPUT_X       0x00U
#define REG_INPUT_Y       0x04U
#define REG_INPUT_ACTION  0x08U
#define REG_INPUT_FLAGS   0x0CU

// System Registers
#define REG_SYS_STATUS    0x00U
#define REG_SYS_BATTERY   0x04U
#define REG_SYS_TICKS     0x08U
#define REG_SYS_COMMAND   0x0CU

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t mode;
    uint32_t* vram;
    uint32_t uptime_ticks;
} kernel_context_t;

void kernel_init(void);
void kernel_step(void);
void kernel_write_uart_string(const char* str);
int kernel_read_uart_line(char* out_buf, size_t max_len);

#endif // SPLITOS_KERNEL_ENTRY_H
