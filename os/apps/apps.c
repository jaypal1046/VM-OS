#include "apps.h"
#include "../kernel/kernel_entry.h"
#include <stdio.h>

void app_dialer_trigger_call(const char* phone_number) {
    if (!phone_number) return;
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "CALL:%s\n", phone_number);
    kernel_write_uart_string(buffer);
}

void app_messages_send_sms(const char* phone_number, const char* message) {
    if (!phone_number || !message) return;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "SMS:%s:%s\n", phone_number, message);
    kernel_write_uart_string(buffer);
}
