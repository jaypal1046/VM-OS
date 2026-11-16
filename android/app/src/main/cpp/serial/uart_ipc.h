#ifndef UART_IPC_H
#define UART_IPC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*uart_ipc_callback_t)(const char* cmd);

void uart_ipc_init(uart_ipc_callback_t callback);
void uart_ipc_cleanup(void);

// Guest -> Host communication
// Oversized or NUL-containing commands are discarded through their line terminator.
void uart_ipc_guest_write(const char* data, size_t len);
void uart_ipc_guest_send_line(const char* line);

// Host -> Guest communication
void uart_ipc_host_write(const char* data, size_t len);
void uart_ipc_host_send_line(const char* line);

// Guest reads pending host messages
int uart_ipc_guest_read(char* out_buf, size_t max_len);
// Consumes a complete line even when the output buffer truncates it.
int uart_ipc_guest_read_line(char* out_buf, size_t max_len);
int uart_ipc_guest_has_data(void);

#ifdef __cplusplus
}
#endif

#endif // UART_IPC_H
