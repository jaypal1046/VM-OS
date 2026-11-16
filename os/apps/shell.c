#include "shell.h"
#include "../kernel/kernel_entry.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static char g_history[SHELL_MAX_LINES][SHELL_LINE_LEN];
static int g_history_count = 0;

static void shell_append_line(const char* line) {
    if (!line) return;
    if (g_history_count < SHELL_MAX_LINES) {
        strncpy(g_history[g_history_count], line, SHELL_LINE_LEN - 1);
        g_history[g_history_count][SHELL_LINE_LEN - 1] = '\0';
        g_history_count++;
    } else {
        // Shift lines up
        for (int i = 0; i < SHELL_MAX_LINES - 1; i++) {
            strncpy(g_history[i], g_history[i + 1], SHELL_LINE_LEN);
        }
        strncpy(g_history[SHELL_MAX_LINES - 1], line, SHELL_LINE_LEN - 1);
        g_history[SHELL_MAX_LINES - 1][SHELL_LINE_LEN - 1] = '\0';
    }
}

void shell_init(void) {
    g_history_count = 0;
    shell_append_line("SplitOS Micro-Shell v1.0 [Ready]");
    shell_append_line("Type 'help' for available commands.");
}

void shell_clear(void) {
    g_history_count = 0;
}

int shell_get_history_count(void) {
    return g_history_count;
}

const char* shell_get_history_line(int index) {
    if (index >= 0 && index < g_history_count) {
        return g_history[index];
    }
    return "";
}

void shell_execute_cmd(const char* cmd_line) {
    if (!cmd_line) return;
    char buf[128];
    strncpy(buf, cmd_line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Trim trailing newline or whitespace
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) {
        buf[--len] = '\0';
    }
    if (len == 0) return;

    // Echo input
    char echo_buf[SHELL_LINE_LEN];
    snprintf(echo_buf, sizeof(echo_buf), "$ %s", buf);
    shell_append_line(echo_buf);

    if (strcmp(buf, "help") == 0) {
        shell_append_line("Commands: help, status, call <num>, sms <num> <msg>");
        shell_append_line("          notif <msg>, mode <mobile|desktop>, mem, clear");
    } else if (strcmp(buf, "clear") == 0) {
        shell_clear();
    } else if (strncmp(buf, "call ", 5) == 0) {
        const char* num = buf + 5;
        char uart_buf[128];
        snprintf(uart_buf, sizeof(uart_buf), "CALL:%s\n", num);
        kernel_write_uart_string(uart_buf);
        char resp[64];
        snprintf(resp, sizeof(resp), "Dialing %s via Android host...", num);
        shell_append_line(resp);
    } else if (strncmp(buf, "sms ", 4) == 0) {
        char* rest = buf + 4;
        char* space = strchr(rest, ' ');
        if (space) {
            *space = '\0';
            const char* num = rest;
            const char* msg = space + 1;
            char uart_buf[256];
            snprintf(uart_buf, sizeof(uart_buf), "SMS:%s:%s\n", num, msg);
            kernel_write_uart_string(uart_buf);
            shell_append_line("SMS queued via Android host.");
        } else {
            shell_append_line("Usage: sms <number> <message>");
        }
    } else if (strncmp(buf, "notif ", 6) == 0) {
        const char* msg = buf + 6;
        char uart_buf[128];
        snprintf(uart_buf, sizeof(uart_buf), "NOTIF:SplitOS Shell:%s\n", msg);
        kernel_write_uart_string(uart_buf);
        shell_append_line("Notification dispatched.");
    } else if (strncmp(buf, "mode ", 5) == 0) {
        const char* mode = buf + 5;
        char uart_buf[64];
        snprintf(uart_buf, sizeof(uart_buf), "SET_MODE:%s\n", mode);
        kernel_write_uart_string(uart_buf);
        shell_append_line("Display mode switch requested.");
    } else if (strcmp(buf, "status") == 0 || strcmp(buf, "sys") == 0) {
        shell_append_line("OS: SplitOS Subsystem 1.0.0");
        shell_append_line("Telephony: ACTIVE | UART: 115200 8N1");
    } else if (strcmp(buf, "mem") == 0) {
        shell_append_line("Guest RAM: 16 MB allocated (< 64 MB target)");
        shell_append_line("VRAM: 720x1280 32-bit RGBA (~3.6 MB)");
    } else {
        char err[64];
        snprintf(err, sizeof(err), "Unknown command: %s", buf);
        shell_append_line(err);
    }
}
