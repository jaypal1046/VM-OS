#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "kernel/kernel_entry.h"
#include "apps/apps.h"
#include "apps/shell.h"
#include "apps/calculator.h"
#include "compositor/compositor.h"

static char g_last_uart_out[256];
static int g_uart_out_count = 0;

// Mock UART driver for host testing
void kernel_write_uart_string(const char* str) {
    if (!str) return;
    strncpy(g_last_uart_out, str, sizeof(g_last_uart_out) - 1);
    g_uart_out_count++;
}

int kernel_read_uart_line(char* out_buf, size_t max_len) {
    (void)out_buf;
    (void)max_len;
    return 0;
}

void test_calculator(void) {
    printf("[1/3] Testing Guest Calculator App...\n");
    calculator_init();
    assert(strcmp(calculator_get_display(), "0") == 0);

    // Test 12 + 34 = 46
    calculator_press_key('1');
    calculator_press_key('2');
    assert(strcmp(calculator_get_display(), "12") == 0);

    calculator_press_key('+');
    calculator_press_key('3');
    calculator_press_key('4');
    assert(strcmp(calculator_get_display(), "34") == 0);

    calculator_press_key('=');
    assert(strcmp(calculator_get_display(), "46") == 0);

    // Test clear
    calculator_press_key('C');
    assert(strcmp(calculator_get_display(), "0") == 0);

    // Test 7 * 6 = 42
    calculator_press_key('7');
    calculator_press_key('*');
    calculator_press_key('6');
    calculator_press_key('=');
    assert(strcmp(calculator_get_display(), "42") == 0);

    calculator_press_key('C');
    const char* large = "100000000000000000000";
    for (const char* digit = large; *digit; digit++) calculator_press_key(*digit);
    calculator_press_key('+');
    calculator_press_key('0');
    calculator_press_key('=');
    assert(strcmp(calculator_get_display(), "1e+20") == 0);

    printf("   -> Calculator OK!\n");
}

void test_shell(void) {
    printf("[2/3] Testing Guest Micro-Shell...\n");
    shell_init();
    assert(shell_get_history_count() > 0);

    shell_execute_cmd("help");
    assert(shell_get_history_count() > 2);

    shell_execute_cmd("status");
    assert(shell_get_history_count() > 3);

    shell_execute_cmd("mem");
    assert(shell_get_history_count() > 4);

    // Test call command via UART
    g_uart_out_count = 0;
    shell_execute_cmd("call 911");
    assert(g_uart_out_count == 1);
    assert(strcmp(g_last_uart_out, "CALL:911\n") == 0);

    // Test sms command via UART
    g_uart_out_count = 0;
    shell_execute_cmd("sms 1234 hello");
    assert(g_uart_out_count == 1);
    assert(strcmp(g_last_uart_out, "SMS:1234:hello\n") == 0);

    shell_execute_cmd("clear");
    assert(shell_get_history_count() == 0);

    shell_execute_cmd("echo hello world");
    assert(shell_get_history_count() == 2);

    printf("   -> Shell OK!\n");
}

void test_compositor(void) {
    printf("[3/3] Testing Guest Compositor...\n");
    guest_compositor_init(0);
    uint32_t fake_fb[100];
    guest_compositor_draw_scene(fake_fb, 10, 10);
    guest_compositor_on_input(0.5f, 0.5f, 0);
    printf("   -> Compositor OK!\n");
}

int main(void) {
    printf("==========================================\n");
    printf(" SplitOS Guest OS System Test Suite       \n");
    printf("==========================================\n");

    test_calculator();
    test_shell();
    test_compositor();

    printf("\n>>> ALL GUEST TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
