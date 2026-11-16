#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "display/display_bridge.h"
#include "serial/uart_ipc.h"
#include "os_compositor.h"
#include "vm_core/vm_engine.h"

static char g_last_dispatched_cmd[256] = {0};
static int g_dispatched_count = 0;

static void mock_jni_uart_callback(const char* cmd) {
    printf("   [Mock JNI Callback] Received guest command: '%s'\n", cmd);
    strncpy(g_last_dispatched_cmd, cmd, sizeof(g_last_dispatched_cmd) - 1);
    g_dispatched_count++;
}

void test_display_bridge(void) {
    printf("[1/5] Testing Display Bridge...\n");
    assert(display_bridge_init(720, 1280) == 0);

    int w = 0, h = 0;
    uint32_t* fb = display_bridge_get_fb(&w, &h);
    assert(fb != NULL);
    assert(w == 720);
    assert(h == 1280);

    // Test writing pixel
    display_bridge_lock_fb();
    fb[0] = 0xFF00FF00;
    display_bridge_unlock_fb();
    assert(fb[0] == 0xFF00FF00);

    // Test resize
    display_bridge_resize(1280, 720);
    fb = display_bridge_get_fb(&w, &h);
    assert(w == 1280);
    assert(h == 720);

    display_bridge_cleanup();
    printf("   -> Display Bridge OK!\n");
}

void test_uart_ipc(void) {
    printf("[2/5] Testing Virtual UART IPC...\n");
    uart_ipc_init(mock_jni_uart_callback);

    // Test Guest -> Host command dispatching
    g_dispatched_count = 0;
    memset(g_last_dispatched_cmd, 0, sizeof(g_last_dispatched_cmd));

    uart_ipc_guest_send_line("CALL:+18005550199");
    assert(g_dispatched_count == 1);
    assert(strcmp(g_last_dispatched_cmd, "CALL:+18005550199") == 0);

    uart_ipc_guest_send_line("SMS:+15551234:Hello from SplitOS!");
    assert(g_dispatched_count == 2);
    assert(strcmp(g_last_dispatched_cmd, "SMS:+15551234:Hello from SplitOS!") == 0);

    // Test Host -> Guest event queue
    uart_ipc_host_send_line("EVENT:BATTERY:88:1");
    assert(uart_ipc_guest_has_data() == 1);

    char rx_line[128] = {0};
    int len = uart_ipc_guest_read_line(rx_line, sizeof(rx_line));
    assert(len > 0);
    assert(strcmp(rx_line, "EVENT:BATTERY:88:1") == 0);
    assert(uart_ipc_guest_has_data() == 0);

    uart_ipc_cleanup();
    printf("   -> Virtual UART IPC OK!\n");
}

void test_compositor_mobile(void) {
    printf("[3/5] Testing Compositor (Mobile Mode & Dialer)...\n");
    uart_ipc_init(mock_jni_uart_callback);
    compositor_init(MODE_MOBILE);
    assert(compositor_get_mode() == MODE_MOBILE);

    display_bridge_init(720, 1280);
    int w = 0, h = 0;
    uint32_t* fb = display_bridge_get_fb(&w, &h);

    // Render initial frame
    compositor_render(fb, w, h);

    // Test incoming call event
    g_dispatched_count = 0;
    compositor_handle_uart_event("EVENT:CALL_INCOMING:+1987654321");
    compositor_render(fb, w, h);

    // Tap on incoming call banner (top area) -> switches to dialer app
    compositor_handle_touch(0.5f, 0.05f, 0); // Touch down
    compositor_handle_touch(0.5f, 0.05f, 1); // Touch up

    // Tap dialer keypad: '9' (row 2, col 2), then '1' (row 0, col 0), then CALL button
    // Tap CALL button (center bottom of dialer)
    g_dispatched_count = 0;
    memset(g_last_dispatched_cmd, 0, sizeof(g_last_dispatched_cmd));

    // Tap call button at (x=360/720=0.5, y=670/1280=0.523)
    compositor_handle_touch(0.5f, 0.525f, 0);
    compositor_handle_touch(0.5f, 0.525f, 1);

    assert(g_dispatched_count >= 1);
    assert(strncmp(g_last_dispatched_cmd, "CALL:", 5) == 0);
    printf("   -> Dispatched dialer call: %s\n", g_last_dispatched_cmd);

    compositor_cleanup();
    display_bridge_cleanup();
    uart_ipc_cleanup();
    printf("   -> Compositor Mobile Mode OK!\n");
}

void test_compositor_desktop(void) {
    printf("[4/5] Testing Compositor (Desktop Mode & Multi-Window)...\n");
    uart_ipc_init(mock_jni_uart_callback);
    compositor_init(MODE_DESKTOP);
    assert(compositor_get_mode() == MODE_DESKTOP);

    display_bridge_init(720, 1280);
    int w = 0, h = 0;
    uint32_t* fb = display_bridge_get_fb(&w, &h);

    // Render initial desktop scene
    compositor_render(fb, w, h);

    // Click on window 0 title bar (x=50/720=0.069, y=90/1280=0.070) and drag to (x=100/720=0.138, y=140/1280=0.109)
    compositor_handle_touch(0.07f, 0.07f, 0); // DOWN
    compositor_handle_touch(0.14f, 0.11f, 2); // MOVE
    compositor_handle_touch(0.14f, 0.11f, 1); // UP

    compositor_render(fb, w, h);

    // Click Start Menu on Taskbar (bottom left) -> switches back to Mobile
    g_dispatched_count = 0;
    compositor_handle_touch(0.05f, 0.98f, 0);
    compositor_handle_touch(0.05f, 0.98f, 1);

    assert(compositor_get_mode() == MODE_MOBILE);
    assert(strcmp(g_last_dispatched_cmd, "SET_MODE:mobile") == 0);

    compositor_cleanup();
    display_bridge_cleanup();
    uart_ipc_cleanup();
    printf("   -> Compositor Desktop Mode OK!\n");
}

void test_vm_engine_lifecycle(void) {
    printf("[5/5] Testing VM Engine Lifecycle & Snapshots...\n");
    uart_ipc_init(mock_jni_uart_callback);

    // Initialize with binary image
    assert(vm_engine_init("android/app/src/main/assets/guest_os.bin") == 0);
    assert(vm_engine_get_state() == VM_STATE_STOPPED);

    // Start VM
    assert(vm_engine_start() == 1);
    assert(vm_engine_get_state() == VM_STATE_RUNNING);

    // Let VM run for ~100ms
    usleep(100000);

    // Pause VM (Snapshot captured)
    assert(vm_engine_pause() == 1);
    assert(vm_engine_get_state() == VM_STATE_PAUSED);

    // Resume VM (Snapshot restored)
    assert(vm_engine_resume() == 1);
    assert(vm_engine_get_state() == VM_STATE_RUNNING);

    // RAM usage check
    uint64_t ram_usage = vm_engine_get_ram_usage();
    printf("   -> VM RAM footprint: %lu bytes (%.2f MB)\n", (unsigned long)ram_usage, (double)ram_usage / (1024.0 * 1024.0));
    assert(ram_usage < 64 * 1024 * 1024); // Must be strictly < 64MB per AGENT_README.md

    // Stop VM
    assert(vm_engine_stop() == 1);
    assert(vm_engine_get_state() == VM_STATE_STOPPED);

    vm_engine_cleanup();
    uart_ipc_cleanup();
    printf("   -> VM Engine Lifecycle OK!\n");
}

int main(void) {
    printf("==========================================\n");
    printf(" SplitOS Native Core Engine Test Suite    \n");
    printf("==========================================\n");

    test_display_bridge();
    test_uart_ipc();
    test_compositor_mobile();
    test_compositor_desktop();
    test_vm_engine_lifecycle();

    printf("\n>>> ALL 5 TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
