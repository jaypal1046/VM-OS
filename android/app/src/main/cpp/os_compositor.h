#ifndef OS_COMPOSITOR_H
#define OS_COMPOSITOR_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODE_MOBILE  0
#define MODE_DESKTOP 1

typedef enum {
    APP_NONE = 0,
    APP_DIALER,
    APP_MESSAGES,
    APP_TERMINAL,
    APP_NOTES,
    APP_SETTINGS
} guest_app_id_t;

typedef struct {
    int id;
    const char* title;
    int x;
    int y;
    int w;
    int h;
    int orig_x;
    int orig_y;
    int orig_w;
    int orig_h;
    int is_minimized;
    int is_maximized;
    int is_visible;
    int z_order;
} desktop_window_t;

typedef struct {
    int mode; // MODE_MOBILE or MODE_DESKTOP
    guest_app_id_t active_mobile_app;

    // Pointer state
    int pointer_x;
    int pointer_y;
    int pointer_down;
    int drag_window_id;
    int drag_offset_x;
    int drag_offset_y;

    // Telephony & Host Status
    int battery_percent;
    int is_charging;
    char incoming_caller[64];
    int is_call_ringing;
    int is_network_connected;

    // Phone Dialer state
    char dialed_number[32];
    size_t dialed_len;

    // SMS state
    char sms_recipient[32];
    char sms_message[128];

    // Terminal state
    char term_history[8][64];
    int term_history_count;

    // Windows for desktop mode
    desktop_window_t windows[4];
    int window_count;
    int focused_window_id;

    // Timing
    uint32_t ticks;
} compositor_state_t;

void compositor_init(int initial_mode);
void compositor_cleanup(void);
void compositor_set_mode(int mode);
int compositor_get_mode(void);

void compositor_handle_touch(float norm_x, float norm_y, int action);
void compositor_handle_uart_event(const char* event_line);
void compositor_tick(void);
void compositor_render(uint32_t* fb, int width, int height);

// Snapshot support
size_t compositor_get_snapshot_size(void);
void compositor_save_snapshot(void* dest);
void compositor_restore_snapshot(const void* src);

#ifdef __cplusplus
}
#endif

#endif // OS_COMPOSITOR_H
