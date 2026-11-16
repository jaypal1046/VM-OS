#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>
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

    int dispatched_before = g_dispatched_count;
    char oversized[1100];
    memset(oversized, 'X', sizeof(oversized));
    uart_ipc_guest_write(oversized, 600);
    uart_ipc_guest_write(oversized + 600, sizeof(oversized) - 600);
    uart_ipc_guest_write("\n", 1);
    assert(g_dispatched_count == dispatched_before);
    const char embedded_null[] = "CALL:123\0ignored\n";
    uart_ipc_guest_write(embedded_null, sizeof(embedded_null) - 1);
    assert(g_dispatched_count == dispatched_before);
    uart_ipc_guest_send_line("NOTIF:Recovery:OK");
    assert(g_dispatched_count == dispatched_before + 1);
    assert(strcmp(g_last_dispatched_cmd, "NOTIF:Recovery:OK") == 0);

    // Test Host -> Guest event queue
    uart_ipc_host_send_line("EVENT:BATTERY:88:1");
    assert(uart_ipc_guest_has_data() == 1);

    char rx_line[128] = {0};
    int len = uart_ipc_guest_read_line(rx_line, sizeof(rx_line));
    assert(len > 0);
    assert(strcmp(rx_line, "EVENT:BATTERY:88:1") == 0);
    assert(uart_ipc_guest_has_data() == 0);

    uart_ipc_host_send_line("a message longer than the output buffer");
    uart_ipc_host_send_line("NEXT");
    char small[5];
    assert(uart_ipc_guest_read_line(small, sizeof(small)) == 4);
    assert(strcmp(small, "a me") == 0);
    assert(uart_ipc_guest_read_line(small, sizeof(small)) == 4);
    assert(strcmp(small, "NEXT") == 0);
    assert(uart_ipc_guest_has_data() == 0);

    uart_ipc_host_write("PART", 4);
    assert(uart_ipc_guest_read_line(small, sizeof(small)) == 0);
    uart_ipc_host_write("\n", 1);
    assert(uart_ipc_guest_read_line(small, sizeof(small)) == 4);
    assert(strcmp(small, "PART") == 0);
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

    compositor_state_t initial, checked;
    compositor_save_snapshot(&initial);
    compositor_handle_touch(NAN, 0.5f, 0);
    compositor_handle_touch(0.5f, INFINITY, 0);
    compositor_handle_touch(-0.1f, 0.5f, 0);
    compositor_handle_touch(1.1f, 0.5f, 0);
    compositor_handle_touch(0.5f, 0.5f, 99);
    compositor_save_snapshot(&checked);
    assert(memcmp(&initial, &checked, sizeof(initial)) == 0);
    compositor_handle_touch(1.0f, 1.0f, 2);
    compositor_save_snapshot(&checked);
    assert(checked.pointer_x == 719 && checked.pointer_y == 1279);
    compositor_restore_snapshot(&initial);

    checked = initial;
    checked.active_mobile_app = APP_CALCULATOR;
    strcpy(checked.calc_display, "42");
    compositor_restore_snapshot(&checked);
    compositor_handle_touch(0.54f, 0.40f, 0);
    compositor_save_snapshot(&checked);
    assert(strcmp(checked.calc_display, "42") == 0);
    checked.calc_acc = 1e20;
    checked.calc_op = '+';
    strcpy(checked.calc_display, "0");
    compositor_restore_snapshot(&checked);
    compositor_handle_touch(0.54f, 0.40f, 0); // Equals key
    compositor_save_snapshot(&checked);
    assert(strcmp(checked.calc_display, "1e+20") == 0);
    compositor_restore_snapshot(&initial);

    // Render initial frame
    compositor_render(fb, w, h);

    // Test incoming call event
    g_dispatched_count = 0;
    compositor_handle_uart_event("EVENT:CALL_INCOMING:+1987654321");
    compositor_render(fb, w, h);

    // Tap on incoming call banner (top area) -> switches to dialer app
    compositor_handle_touch(0.5f, 0.05f, 0); // Touch down
    compositor_handle_touch(0.5f, 0.05f, 1); // Touch up

    // Empty dialer must not dispatch a call.
    g_dispatched_count = 0;
    compositor_handle_touch(0.5f, 0.525f, 0);
    compositor_handle_touch(0.5f, 0.525f, 1);
    assert(g_dispatched_count == 0);

    // Enter 1 then call; callback is mocked, so no real phone action occurs.
    compositor_handle_touch(0.30f, 0.26f, 0);
    compositor_handle_touch(0.30f, 0.26f, 1);
    compositor_handle_touch(0.5f, 0.525f, 0);
    compositor_handle_touch(0.5f, 0.525f, 1);
    assert(g_dispatched_count == 1);
    assert(strcmp(g_last_dispatched_cmd, "CALL:1") == 0);

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

static void* exercise_lifecycle(void* unused) {
    (void)unused;
    for (int i = 0; i < 20; i++) {
        assert(vm_engine_start() == 1);
        usleep(1000);
        assert(vm_engine_stop() == 1);
    }
    return NULL;
}

void test_vm_engine_lifecycle(void) {
    printf("[5/5] Testing VM Engine Lifecycle & Snapshots...\n");
    uart_ipc_init(mock_jni_uart_callback);

    assert(vm_engine_init("/nonexistent/splitos/guest_os.bin") == -1);
    assert(vm_engine_init("README.md") == -1);
    assert(vm_engine_get_state() == VM_STATE_STOPPED);

    // Initialize with binary image
    assert(vm_engine_init("android/app/src/main/assets/guest_os.bin") == 0);
    assert(vm_engine_get_state() == VM_STATE_STOPPED);

    // Start VM
    assert(vm_engine_start() == 1);
    assert(vm_engine_get_state() == VM_STATE_RUNNING);

    assert(vm_engine_start() == 1);
    assert(vm_engine_init(NULL) == -1);
    assert(vm_engine_get_state() == VM_STATE_RUNNING);

    // Let VM run for ~100ms
    usleep(100000);

    // Pause VM (Snapshot captured)
    assert(vm_engine_pause() == 1);
    assert(vm_engine_get_state() == VM_STATE_PAUSED);

    assert(vm_engine_start() == 0);
    assert(vm_engine_init(NULL) == -1);
    assert(vm_engine_get_state() == VM_STATE_PAUSED);

    compositor_state_t before, after;
    compositor_save_snapshot(&before);
    vm_engine_send_touch(0.5f, 0.5f, 0);
    usleep(50000);
    compositor_save_snapshot(&after);
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    vm_engine_set_display_mode(MODE_DESKTOP);
    assert(vm_engine_get_display_mode() == MODE_DESKTOP);
    vm_engine_set_display_mode(99);
    assert(vm_engine_get_display_mode() == MODE_DESKTOP);

    // Resume VM (Snapshot restored)
    assert(vm_engine_resume() == 1);
    assert(vm_engine_get_state() == VM_STATE_RUNNING);
    assert(vm_engine_get_display_mode() == MODE_DESKTOP);

    // RAM usage check
    uint64_t ram_usage = vm_engine_get_ram_usage();
    printf("   -> VM RAM footprint: %lu bytes (%.2f MB)\n", (unsigned long)ram_usage, (double)ram_usage / (1024.0 * 1024.0));
    assert(ram_usage >= 2ULL * GUEST_RAM_SIZE + 720ULL * 1280 * sizeof(uint32_t)
                        + 2 * compositor_get_snapshot_size());
    assert(ram_usage < 64 * 1024 * 1024); // Must be strictly < 64MB per AGENT_README.md

    // Stop VM
    assert(vm_engine_stop() == 1);
    assert(vm_engine_get_state() == VM_STATE_STOPPED);

    // A paused worker must wake and join on stop, then allow a fresh start.
    assert(vm_engine_start() == 1);
    assert(vm_engine_pause() == 1);
    assert(vm_engine_stop() == 1);
    pthread_t callers[2];
    assert(pthread_create(&callers[0], NULL, exercise_lifecycle, NULL) == 0);
    assert(pthread_create(&callers[1], NULL, exercise_lifecycle, NULL) == 0);
    assert(pthread_join(callers[0], NULL) == 0);
    assert(pthread_join(callers[1], NULL) == 0);
    assert(vm_engine_get_state() == VM_STATE_STOPPED);
    vm_engine_cleanup();
    assert(vm_engine_get_ram_usage() < GUEST_RAM_SIZE);
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
