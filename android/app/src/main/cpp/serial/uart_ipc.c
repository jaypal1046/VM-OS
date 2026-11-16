#include "uart_ipc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "UartIpc"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) printf("[UartIpc INFO] " __VA_ARGS__); printf("\n")
#define LOGD(...) printf("[UartIpc DEBUG] " __VA_ARGS__); printf("\n")
#define LOGE(...) fprintf(stderr, "[UartIpc ERROR] " __VA_ARGS__); fprintf(stderr, "\n")
#endif

#define UART_BUFFER_SIZE 4096

// Ring buffer for host-to-guest messages
typedef struct {
    char data[UART_BUFFER_SIZE];
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
} ring_buffer_t;

// Accumulator for guest-to-host commands
typedef struct {
    char line[1024];
    size_t len;
    int discarding;
    pthread_mutex_t mutex;
} line_buffer_t;

static ring_buffer_t g_host_to_guest_rx;
static line_buffer_t g_guest_to_host_tx;
static uart_ipc_callback_t g_host_callback = NULL;
static pthread_mutex_t g_callback_mutex = PTHREAD_MUTEX_INITIALIZER;

void uart_ipc_init(uart_ipc_callback_t callback) {
    pthread_mutex_lock(&g_callback_mutex);
    g_host_callback = callback;
    pthread_mutex_unlock(&g_callback_mutex);

    pthread_mutex_init(&g_host_to_guest_rx.mutex, NULL);
    g_host_to_guest_rx.head = 0;
    g_host_to_guest_rx.tail = 0;
    g_host_to_guest_rx.count = 0;

    pthread_mutex_init(&g_guest_to_host_tx.mutex, NULL);
    g_guest_to_host_tx.len = 0;
    g_guest_to_host_tx.discarding = 0;

    LOGI("UART IPC initialized.");
}

void uart_ipc_cleanup(void) {
    pthread_mutex_lock(&g_callback_mutex);
    g_host_callback = NULL;
    pthread_mutex_unlock(&g_callback_mutex);

    pthread_mutex_destroy(&g_host_to_guest_rx.mutex);
    pthread_mutex_destroy(&g_guest_to_host_tx.mutex);
    LOGI("UART IPC cleaned up.");
}

void uart_ipc_guest_write(const char* data, size_t len) {
    if (!data || len == 0) return;

    pthread_mutex_lock(&g_guest_to_host_tx.mutex);
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n' || c == '\r') {
            if (g_guest_to_host_tx.len > 0 && !g_guest_to_host_tx.discarding) {
                g_guest_to_host_tx.line[g_guest_to_host_tx.len] = '\0';

                // Dispatch to host
                pthread_mutex_lock(&g_callback_mutex);
                uart_ipc_callback_t cb = g_host_callback;
                pthread_mutex_unlock(&g_callback_mutex);

                if (cb) {
                    cb(g_guest_to_host_tx.line);
                }
            }
            g_guest_to_host_tx.len = 0;
            g_guest_to_host_tx.discarding = 0;
        } else {
            if (!g_guest_to_host_tx.discarding && c != '\0' &&
                g_guest_to_host_tx.len < sizeof(g_guest_to_host_tx.line) - 1) {
                g_guest_to_host_tx.line[g_guest_to_host_tx.len++] = c;
            } else {
                // Reject the entire command rather than dispatching a partial request.
                g_guest_to_host_tx.discarding = 1;
            }
        }
    }
    pthread_mutex_unlock(&g_guest_to_host_tx.mutex);
}

void uart_ipc_guest_send_line(const char* line) {
    if (!line) return;
    size_t len = strlen(line);
    uart_ipc_guest_write(line, len);
    uart_ipc_guest_write("\n", 1);
}

void uart_ipc_host_write(const char* data, size_t len) {
    if (!data || len == 0) return;

    pthread_mutex_lock(&g_host_to_guest_rx.mutex);
    for (size_t i = 0; i < len; i++) {
        if (g_host_to_guest_rx.count < UART_BUFFER_SIZE) {
            g_host_to_guest_rx.data[g_host_to_guest_rx.head] = data[i];
            g_host_to_guest_rx.head = (g_host_to_guest_rx.head + 1) % UART_BUFFER_SIZE;
            g_host_to_guest_rx.count++;
        } else {
            // Buffer overflow: overwrite oldest byte
            g_host_to_guest_rx.tail = (g_host_to_guest_rx.tail + 1) % UART_BUFFER_SIZE;
            g_host_to_guest_rx.data[g_host_to_guest_rx.head] = data[i];
            g_host_to_guest_rx.head = (g_host_to_guest_rx.head + 1) % UART_BUFFER_SIZE;
        }
    }
    pthread_mutex_unlock(&g_host_to_guest_rx.mutex);
}

void uart_ipc_host_send_line(const char* line) {
    if (!line) return;
    size_t len = strlen(line);
    uart_ipc_host_write(line, len);
    uart_ipc_host_write("\n", 1);
}

int uart_ipc_guest_read(char* out_buf, size_t max_len) {
    if (!out_buf || max_len == 0) return 0;

    pthread_mutex_lock(&g_host_to_guest_rx.mutex);
    size_t read_bytes = 0;
    while (read_bytes < max_len && g_host_to_guest_rx.count > 0) {
        out_buf[read_bytes++] = g_host_to_guest_rx.data[g_host_to_guest_rx.tail];
        g_host_to_guest_rx.tail = (g_host_to_guest_rx.tail + 1) % UART_BUFFER_SIZE;
        g_host_to_guest_rx.count--;
    }
    pthread_mutex_unlock(&g_host_to_guest_rx.mutex);
    return (int)read_bytes;
}

int uart_ipc_guest_read_line(char* out_buf, size_t max_len) {
    if (!out_buf || max_len == 0) return 0;

    pthread_mutex_lock(&g_host_to_guest_rx.mutex);
    // Check if newline exists in ring buffer
    size_t count = g_host_to_guest_rx.count;
    size_t curr = g_host_to_guest_rx.tail;
    int found_newline = 0;
    size_t line_len = 0;

    for (size_t i = 0; i < count; i++) {
        char c = g_host_to_guest_rx.data[curr];
        if (c == '\n' || c == '\r') {
            found_newline = 1;
            line_len = i;
            break;
        }
        curr = (curr + 1) % UART_BUFFER_SIZE;
    }

    if (!found_newline) {
        pthread_mutex_unlock(&g_host_to_guest_rx.mutex);
        return 0; // Incomplete line
    }

    size_t copy_len = line_len < max_len - 1 ? line_len : max_len - 1;
    for (size_t i = 0; i < line_len; i++) {
        if (i < copy_len) out_buf[i] = g_host_to_guest_rx.data[g_host_to_guest_rx.tail];
        g_host_to_guest_rx.tail = (g_host_to_guest_rx.tail + 1) % UART_BUFFER_SIZE;
        g_host_to_guest_rx.count--;
    }
    out_buf[copy_len] = '\0';

    // Discard trailing newline chars (\r and \n)
    while (g_host_to_guest_rx.count > 0) {
        char c = g_host_to_guest_rx.data[g_host_to_guest_rx.tail];
        if (c == '\n' || c == '\r') {
            g_host_to_guest_rx.tail = (g_host_to_guest_rx.tail + 1) % UART_BUFFER_SIZE;
            g_host_to_guest_rx.count--;
        } else {
            break;
        }
    }

    pthread_mutex_unlock(&g_host_to_guest_rx.mutex);
    return (int)copy_len;
}

int uart_ipc_guest_has_data(void) {
    pthread_mutex_lock(&g_host_to_guest_rx.mutex);
    int has = g_host_to_guest_rx.count > 0;
    pthread_mutex_unlock(&g_host_to_guest_rx.mutex);
    return has;
}
