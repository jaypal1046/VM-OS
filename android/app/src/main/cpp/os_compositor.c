#include "os_compositor.h"
#include "font8x8.h"
#include "serial/uart_ipc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "OsCompositor"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) printf("[OsCompositor] " __VA_ARGS__); printf("\n")
#endif

// RGBA 32-bit colors (Little Endian: 0xAABBGGRR)
#define COLOR_BG_DARK       0xFF181412
#define COLOR_SURFACE_DARK  0xFF2B221E
#define COLOR_PANEL_BG      0xFF352B25
#define COLOR_ACCENT_BLUE   0xFFFF7A00
#define COLOR_ACCENT_GREEN  0xFF59C734
#define COLOR_ACCENT_RED    0xFF303BFF
#define COLOR_ACCENT_AMBER  0xFF0095FF
#define COLOR_TEXT_PRIMARY  0xFFF5F2F0
#define COLOR_TEXT_MUTED    0xFFB3A298
#define COLOR_WHITE         0xFFFFFFFF
#define COLOR_BLACK         0xFF000000

static compositor_state_t g_state;

// --- 2D Drawing Primitives ---

static inline void put_pixel(uint32_t* fb, int fb_w, int fb_h, int x, int y, uint32_t color) {
    if (x >= 0 && x < fb_w && y >= 0 && y < fb_h) {
        fb[y * fb_w + x] = color;
    }
}

static void draw_rect(uint32_t* fb, int fb_w, int fb_h, int x, int y, int w, int h, uint32_t color) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w) > fb_w ? fb_w : (x + w);
    int y1 = (y + h) > fb_h ? fb_h : (y + h);

    for (int cy = y0; cy < y1; cy++) {
        uint32_t* row = fb + (cy * fb_w);
        for (int cx = x0; cx < x1; cx++) {
            row[cx] = color;
        }
    }
}

static void draw_rect_outline(uint32_t* fb, int fb_w, int fb_h, int x, int y, int w, int h, uint32_t color) {
    draw_rect(fb, fb_w, fb_h, x, y, w, 1, color);
    draw_rect(fb, fb_w, fb_h, x, y + h - 1, w, 1, color);
    draw_rect(fb, fb_w, fb_h, x, y, 1, h, color);
    draw_rect(fb, fb_w, fb_h, x + w - 1, y, 1, h, color);
}

static void draw_char(uint32_t* fb, int fb_w, int fb_h, int x, int y, char c, uint32_t color, int scale) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = font8x8[(int)(c - 32)];

    for (int cy = 0; cy < 8; cy++) {
        uint8_t row = glyph[cy];
        for (int cx = 0; cx < 8; cx++) {
            if (row & (1 << cx)) {
                if (scale == 1) {
                    put_pixel(fb, fb_w, fb_h, x + cx, y + cy, color);
                } else {
                    draw_rect(fb, fb_w, fb_h, x + cx * scale, y + cy * scale, scale, scale, color);
                }
            }
        }
    }
}

static void draw_string(uint32_t* fb, int fb_w, int fb_h, int x, int y, const char* str, uint32_t color, int scale) {
    if (!str) return;
    int cur_x = x;
    while (*str) {
        draw_char(fb, fb_w, fb_h, cur_x, y, *str, color, scale);
        cur_x += 8 * scale;
        str++;
    }
}

static void draw_mouse_cursor(uint32_t* fb, int fb_w, int fb_h, int mx, int my) {
    // 11x17 arrow cursor
    static const uint16_t cursor_mask[16] = {
        0x8000, 0xC000, 0xE000, 0xF000,
        0xF800, 0xFC00, 0xFE00, 0xFF00,
        0xFF80, 0xF800, 0xDC00, 0x8E00,
        0x0700, 0x0380, 0x01C0, 0x0080
    };

    for (int y = 0; y < 16; y++) {
        uint16_t row = cursor_mask[y];
        for (int x = 0; x < 16; x++) {
            if (row & (0x8000 >> x)) {
                put_pixel(fb, fb_w, fb_h, mx + x, my + y, COLOR_WHITE);
            }
        }
    }
}

// --- Compositor Lifecycle ---

void compositor_init(int initial_mode) {
    memset(&g_state, 0, sizeof(g_state));
    g_state.mode = initial_mode;
    g_state.active_mobile_app = APP_NONE;
    g_state.battery_percent = 95;
    g_state.is_charging = 1;
    g_state.is_network_connected = 1;
    g_state.pointer_x = 360;
    g_state.pointer_y = 640;
    g_state.drag_window_id = -1;

    // Terminal default history
    strcpy(g_state.term_history[0], "SplitOS Subsystem v1.0.0 [Ready]");
    strcpy(g_state.term_history[1], "Telephony & SMS Bridge: ONLINE");
    strcpy(g_state.term_history[2], "UART IPC Link: CONNECTED");
    g_state.term_history_count = 3;

    // Default SMS info
    strcpy(g_state.sms_recipient, "+18005550199");
    strcpy(g_state.sms_message, "SplitOS: Mobile & Desktop running safely!");

    // Default Dialed Number
    strcpy(g_state.dialed_number, "911");
    g_state.dialed_len = 3;

    // Setup Desktop Windows
    g_state.window_count = 3;

    // Window 0: Phone Dialer
    g_state.windows[0].id = 0;
    g_state.windows[0].title = "Phone Dialer";
    g_state.windows[0].x = 40;
    g_state.windows[0].y = 80;
    g_state.windows[0].w = 280;
    g_state.windows[0].h = 380;
    g_state.windows[0].orig_x = 40;
    g_state.windows[0].orig_y = 80;
    g_state.windows[0].orig_w = 280;
    g_state.windows[0].orig_h = 380;
    g_state.windows[0].is_visible = 1;
    g_state.windows[0].z_order = 0;

    // Window 1: Terminal
    g_state.windows[1].id = 1;
    g_state.windows[1].title = "SplitOS Terminal";
    g_state.windows[1].x = 350;
    g_state.windows[1].y = 80;
    g_state.windows[1].w = 400;
    g_state.windows[1].h = 280;
    g_state.windows[1].orig_x = 350;
    g_state.windows[1].orig_y = 80;
    g_state.windows[1].orig_w = 400;
    g_state.windows[1].orig_h = 280;
    g_state.windows[1].is_visible = 1;
    g_state.windows[1].z_order = 1;

    // Window 2: System Monitor
    g_state.windows[2].id = 2;
    g_state.windows[2].title = "System Monitor";
    g_state.windows[2].x = 350;
    g_state.windows[2].y = 380;
    g_state.windows[2].w = 400;
    g_state.windows[2].h = 220;
    g_state.windows[2].orig_x = 350;
    g_state.windows[2].orig_y = 380;
    g_state.windows[2].orig_w = 400;
    g_state.windows[2].orig_h = 220;
    g_state.windows[2].is_visible = 1;
    g_state.windows[2].z_order = 2;

    g_state.focused_window_id = 0;

    LOGI("Compositor initialized in mode: %s", initial_mode == MODE_MOBILE ? "Mobile" : "Desktop");
}

void compositor_cleanup(void) {
    LOGI("Compositor cleaned up.");
}

void compositor_set_mode(int mode) {
    g_state.mode = mode;
    LOGI("Compositor mode set to: %d", mode);
}

int compositor_get_mode(void) {
    return g_state.mode;
}

void compositor_handle_uart_event(const char* event_line) {
    if (!event_line) return;

    if (strncmp(event_line, "EVENT:BATTERY:", 14) == 0) {
        int pct = 0, chg = 0;
        if (sscanf(event_line + 14, "%d:%d", &pct, &chg) >= 1) {
            g_state.battery_percent = pct;
            g_state.is_charging = chg;
        }
    } else if (strncmp(event_line, "EVENT:CALL_INCOMING:", 20) == 0 ||
               strncmp(event_line, "EVENT:CALL_RINGING:", 19) == 0) {
        const char* p = strchr(event_line, ':');
        if (p) p = strchr(p + 1, ':');
        if (p) {
            strncpy(g_state.incoming_caller, p + 1, sizeof(g_state.incoming_caller) - 1);
            g_state.is_call_ringing = 1;
        }
    } else if (strcmp(event_line, "EVENT:CALL_IDLE") == 0) {
        g_state.is_call_ringing = 0;
        g_state.incoming_caller[0] = '\0';
    } else if (strcmp(event_line, "EVENT:NETWORK:CONNECTED") == 0) {
        g_state.is_network_connected = 1;
    } else if (strcmp(event_line, "EVENT:NETWORK:DISCONNECTED") == 0) {
        g_state.is_network_connected = 0;
    } else if (strncmp(event_line, "EVENT:DISPLAY_MODE:", 19) == 0) {
        if (strstr(event_line, "desktop")) {
            g_state.mode = MODE_DESKTOP;
        } else {
            g_state.mode = MODE_MOBILE;
        }
    }
}

void compositor_tick(void) {
    g_state.ticks++;

    // Check UART pending lines
    char line[128];
    while (uart_ipc_guest_read_line(line, sizeof(line)) > 0) {
        compositor_handle_uart_event(line);
    }
}

// --- Hit Testing & Input Forwarding ---

static int is_inside(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px < rx + rw && py >= ry && py < ry + rh);
}

static void handle_dialpad_press(char key) {
    if (key == '<') {
        if (g_state.dialed_len > 0) {
            g_state.dialed_number[--g_state.dialed_len] = '\0';
        }
    } else if (key == 'C') {
        // CALL button pressed! Emit UART command to host
        if (g_state.dialed_len > 0) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "CALL:%s", g_state.dialed_number);
            uart_ipc_guest_send_line(cmd);
        }
    } else {
        if (g_state.dialed_len < sizeof(g_state.dialed_number) - 1) {
            g_state.dialed_number[g_state.dialed_len++] = key;
            g_state.dialed_number[g_state.dialed_len] = '\0';
        }
    }
}

void compositor_handle_touch(float norm_x, float norm_y, int action) {
    int fb_w = 720;
    int fb_h = 1280;
    int px = (int)(norm_x * fb_w);
    int py = (int)(norm_y * fb_h);

    g_state.pointer_x = px;
    g_state.pointer_y = py;

    if (action == 0) { // ACTION_DOWN
        g_state.pointer_down = 1;

        if (g_state.mode == MODE_MOBILE) {
            // Dismiss incoming call banner if tapped
            if (g_state.is_call_ringing && py < 100) {
                g_state.active_mobile_app = APP_DIALER;
                return;
            }

            // Bottom Navigation Bar
            if (py >= fb_h - 70) {
                if (px < fb_w / 3) {
                    // Back
                    g_state.active_mobile_app = APP_NONE;
                } else if (px < (fb_w * 2) / 3) {
                    // Home
                    g_state.active_mobile_app = APP_NONE;
                } else {
                    // Switch to Desktop
                    g_state.mode = MODE_DESKTOP;
                    uart_ipc_guest_send_line("SET_MODE:desktop");
                }
                return;
            }

            // Check if inside Home Screen App Grid
            if (g_state.active_mobile_app == APP_NONE) {
                int start_y = 140;
                int icon_spacing = 90;

                if (is_inside(px, py, 60, start_y, fb_w - 120, 64)) {
                    g_state.active_mobile_app = APP_DIALER;
                } else if (is_inside(px, py, 60, start_y + icon_spacing, fb_w - 120, 64)) {
                    g_state.active_mobile_app = APP_MESSAGES;
                } else if (is_inside(px, py, 60, start_y + icon_spacing * 2, fb_w - 120, 64)) {
                    g_state.active_mobile_app = APP_TERMINAL;
                } else if (is_inside(px, py, 60, start_y + icon_spacing * 3, fb_w - 120, 64)) {
                    g_state.active_mobile_app = APP_NOTES;
                } else if (is_inside(px, py, 60, start_y + icon_spacing * 4, fb_w - 120, 64)) {
                    g_state.active_mobile_app = APP_SETTINGS;
                }
            } else if (g_state.active_mobile_app == APP_DIALER) {
                // Keypad layout
                static const char keys[4][3] = {
                    {'1', '2', '3'},
                    {'4', '5', '6'},
                    {'7', '8', '9'},
                    {'*', '0', '#'}
                };
                int pad_y = 300;
                int btn_w = 90;
                int btn_h = 70;
                int start_x = 180;

                for (int r = 0; r < 4; r++) {
                    for (int c = 0; c < 3; c++) {
                        int bx = start_x + c * (btn_w + 20);
                        int by = pad_y + r * (btn_h + 16);
                        if (is_inside(px, py, bx, by, btn_w, btn_h)) {
                            handle_dialpad_press(keys[r][c]);
                            return;
                        }
                    }
                }

                // Backspace
                if (is_inside(px, py, start_x + 3 * (btn_w + 20) - 20, 200, 70, 50)) {
                    handle_dialpad_press('<');
                    return;
                }

                // Call button
                if (is_inside(px, py, 160, 640, fb_w - 320, 75)) {
                    handle_dialpad_press('C');
                    return;
                }
            } else if (g_state.active_mobile_app == APP_MESSAGES) {
                // Send SMS button
                if (is_inside(px, py, 60, 400, fb_w - 120, 60)) {
                    char cmd[256];
                    snprintf(cmd, sizeof(cmd), "SMS:%s:%s", g_state.sms_recipient, g_state.sms_message);
                    uart_ipc_guest_send_line(cmd);
                }
            }
        } else {
            // DESKTOP MODE
            // Taskbar Start Menu
            if (py >= fb_h - 48) {
                if (px < 140) {
                    // Start Menu clicked -> Switch back to Mobile
                    g_state.mode = MODE_MOBILE;
                    uart_ipc_guest_send_line("SET_MODE:mobile");
                }
                return;
            }

            // Window click & drag
            for (int i = g_state.window_count - 1; i >= 0; i--) {
                desktop_window_t* win = &g_state.windows[i];
                if (!win->is_visible) continue;

                if (is_inside(px, py, win->x, win->y, win->w, 32)) {
                    // Clicked in title bar -> check window controls
                    if (is_inside(px, py, win->x + win->w - 30, win->y, 30, 32)) {
                        // Close [ X ]
                        win->is_visible = 0;
                        return;
                    } else if (is_inside(px, py, win->x + win->w - 60, win->y, 30, 32)) {
                        // Maximize [ + ]
                        win->is_maximized = !win->is_maximized;
                        if (win->is_maximized) {
                            win->x = 0;
                            win->y = 0;
                            win->w = fb_w;
                            win->h = fb_h - 48;
                        } else {
                            win->x = win->orig_x;
                            win->y = win->orig_y;
                            win->w = win->orig_w;
                            win->h = win->orig_h;
                        }
                        return;
                    }

                    // Start dragging window
                    g_state.drag_window_id = i;
                    g_state.drag_offset_x = px - win->x;
                    g_state.drag_offset_y = py - win->y;
                    g_state.focused_window_id = i;
                    return;
                } else if (is_inside(px, py, win->x, win->y, win->w, win->h)) {
                    // Clicked inside window body -> focus window
                    g_state.focused_window_id = i;

                    if (win->id == 0) { // Dialer window
                        int pad_x = win->x + 20;
                        int pad_y = win->y + 90;
                        int bw = 65;
                        int bh = 45;
                        static const char keys[4][3] = {
                            {'1', '2', '3'},
                            {'4', '5', '6'},
                            {'7', '8', '9'},
                            {'*', '0', '#'}
                        };
                        for (int r = 0; r < 4; r++) {
                            for (int c = 0; c < 3; c++) {
                                int bx = pad_x + c * (bw + 10);
                                int by = pad_y + r * (bh + 8);
                                if (is_inside(px, py, bx, by, bw, bh)) {
                                    handle_dialpad_press(keys[r][c]);
                                    return;
                                }
                            }
                        }
                        // Call button in desktop window
                        if (is_inside(px, py, win->x + 30, win->y + 315, win->w - 60, 40)) {
                            handle_dialpad_press('C');
                            return;
                        }
                    }
                    return;
                }
            }
        }
    } else if (action == 2) { // ACTION_MOVE
        if (g_state.mode == MODE_DESKTOP && g_state.drag_window_id >= 0) {
            desktop_window_t* win = &g_state.windows[g_state.drag_window_id];
            if (!win->is_maximized) {
                win->x = px - g_state.drag_offset_x;
                win->y = py - g_state.drag_offset_y;
                if (win->x < 0) win->x = 0;
                if (win->y < 0) win->y = 0;
                if (win->x + win->w > fb_w) win->x = fb_w - win->w;
                if (win->y + win->h > fb_h - 48) win->y = fb_h - 48 - win->h;
                win->orig_x = win->x;
                win->orig_y = win->y;
            }
        }
    } else if (action == 1) { // ACTION_UP
        g_state.pointer_down = 0;
        g_state.drag_window_id = -1;
    }
}

// --- Rendering Subsystem ---

static void render_status_bar(uint32_t* fb, int fb_w, int fb_h) {
    draw_rect(fb, fb_w, fb_h, 0, 0, fb_w, 36, COLOR_SURFACE_DARK);
    draw_string(fb, fb_w, fb_h, 16, 10, "SplitOS 1.0", COLOR_ACCENT_BLUE, 2);

    // Battery & Status info
    char bat_str[32];
    snprintf(bat_str, sizeof(bat_str), "%s %d%%", g_state.is_charging ? "CHG" : "BAT", g_state.battery_percent);
    draw_string(fb, fb_w, fb_h, fb_w - 180, 10, bat_str, COLOR_TEXT_PRIMARY, 2);

    // Network status
    draw_string(fb, fb_w, fb_h, fb_w - 70, 10, g_state.is_network_connected ? "4G" : "--", COLOR_ACCENT_GREEN, 2);
}

static void render_incoming_call_banner(uint32_t* fb, int fb_w, int fb_h) {
    if (!g_state.is_call_ringing) return;

    // Glowing notification overlay
    draw_rect(fb, fb_w, fb_h, 20, 46, fb_w - 40, 72, COLOR_ACCENT_GREEN);
    draw_string(fb, fb_w, fb_h, 36, 56, "INCOMING CALL (HOST DIALER)", COLOR_WHITE, 2);
    char caller_str[64];
    snprintf(caller_str, sizeof(caller_str), "Caller: %s", g_state.incoming_caller[0] ? g_state.incoming_caller : "Unknown");
    draw_string(fb, fb_w, fb_h, 36, 82, caller_str, COLOR_WHITE, 2);
}

static void render_mobile_home(uint32_t* fb, int fb_w, int fb_h) {
    draw_string(fb, fb_w, fb_h, 60, 80, "Applications", COLOR_TEXT_MUTED, 2);

    const char* app_names[5] = {"1. Phone Dialer", "2. SMS Messages", "3. System Terminal", "4. Quick Notes", "5. Settings & Info"};
    const uint32_t app_colors[5] = {COLOR_ACCENT_GREEN, COLOR_ACCENT_BLUE, COLOR_SURFACE_DARK, COLOR_ACCENT_AMBER, COLOR_PANEL_BG};

    int start_y = 140;
    int spacing = 90;
    for (int i = 0; i < 5; i++) {
        draw_rect(fb, fb_w, fb_h, 60, start_y + i * spacing, fb_w - 120, 64, app_colors[i]);
        draw_rect_outline(fb, fb_w, fb_h, 60, start_y + i * spacing, fb_w - 120, 64, COLOR_TEXT_MUTED);
        draw_string(fb, fb_w, fb_h, 90, start_y + i * spacing + 22, app_names[i], COLOR_WHITE, 2);
    }
}

static void render_mobile_dialer(uint32_t* fb, int fb_w, int fb_h) {
    draw_string(fb, fb_w, fb_h, 60, 60, "< Back to Apps", COLOR_ACCENT_BLUE, 2);

    // Number display
    draw_rect(fb, fb_w, fb_h, 60, 110, fb_w - 120, 70, COLOR_SURFACE_DARK);
    draw_rect_outline(fb, fb_w, fb_h, 60, 110, fb_w - 120, 70, COLOR_TEXT_MUTED);
    draw_string(fb, fb_w, fb_h, 80, 130, g_state.dialed_number[0] ? g_state.dialed_number : "Enter Number...", COLOR_WHITE, 3);

    // Keypad
    static const char keys[4][3] = {
        {'1', '2', '3'},
        {'4', '5', '6'},
        {'7', '8', '9'},
        {'*', '0', '#'}
    };
    int pad_y = 220;
    int btn_w = 110;
    int btn_h = 75;
    int start_x = 130;

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 3; c++) {
            int bx = start_x + c * (btn_w + 30);
            int by = pad_y + r * (btn_h + 20);
            draw_rect(fb, fb_w, fb_h, bx, by, btn_w, btn_h, COLOR_SURFACE_DARK);
            draw_rect_outline(fb, fb_w, fb_h, bx, by, btn_w, btn_h, COLOR_TEXT_MUTED);
            char key_str[2] = {keys[r][c], '\0'};
            draw_string(fb, fb_w, fb_h, bx + btn_w / 2 - 12, by + btn_h / 2 - 12, key_str, COLOR_WHITE, 3);
        }
    }

    // Call button
    draw_rect(fb, fb_w, fb_h, 160, 640, fb_w - 320, 75, COLOR_ACCENT_GREEN);
    draw_string(fb, fb_w, fb_h, fb_w / 2 - 50, 665, "CALL", COLOR_WHITE, 3);
}

static void render_mobile_messages(uint32_t* fb, int fb_w, int fb_h) {
    draw_string(fb, fb_w, fb_h, 60, 60, "< Back to Apps", COLOR_ACCENT_BLUE, 2);

    draw_string(fb, fb_w, fb_h, 60, 120, "Recipient:", COLOR_TEXT_MUTED, 2);
    draw_rect(fb, fb_w, fb_h, 60, 150, fb_w - 120, 50, COLOR_SURFACE_DARK);
    draw_string(fb, fb_w, fb_h, 80, 165, g_state.sms_recipient, COLOR_WHITE, 2);

    draw_string(fb, fb_w, fb_h, 60, 230, "Message Payload:", COLOR_TEXT_MUTED, 2);
    draw_rect(fb, fb_w, fb_h, 60, 260, fb_w - 120, 100, COLOR_SURFACE_DARK);
    draw_string(fb, fb_w, fb_h, 80, 280, g_state.sms_message, COLOR_WHITE, 2);

    draw_rect(fb, fb_w, fb_h, 120, 400, fb_w - 240, 60, COLOR_ACCENT_BLUE);
    draw_string(fb, fb_w, fb_h, fb_w / 2 - 80, 420, "SEND VIA HOST", COLOR_WHITE, 2);
}

static void render_mobile_terminal(uint32_t* fb, int fb_w, int fb_h) {
    draw_string(fb, fb_w, fb_h, 60, 60, "< Back to Apps", COLOR_ACCENT_BLUE, 2);

    draw_rect(fb, fb_w, fb_h, 40, 110, fb_w - 80, 480, COLOR_BLACK);
    draw_rect_outline(fb, fb_w, fb_h, 40, 110, fb_w - 80, 480, COLOR_ACCENT_GREEN);

    for (int i = 0; i < g_state.term_history_count; i++) {
        draw_string(fb, fb_w, fb_h, 60, 140 + i * 36, g_state.term_history[i], COLOR_ACCENT_GREEN, 2);
    }
    draw_string(fb, fb_w, fb_h, 60, 140 + g_state.term_history_count * 36, "$ ready", COLOR_WHITE, 2);
}

static void render_mobile_nav_bar(uint32_t* fb, int fb_w, int fb_h) {
    draw_rect(fb, fb_w, fb_h, 0, fb_h - 70, fb_w, 70, COLOR_SURFACE_DARK);
    draw_string(fb, fb_w, fb_h, 60, fb_h - 45, "[ < ] Back", COLOR_TEXT_PRIMARY, 2);
    draw_string(fb, fb_w, fb_h, fb_w / 2 - 40, fb_h - 45, "[ O ] Home", COLOR_TEXT_PRIMARY, 2);
    draw_string(fb, fb_w, fb_h, fb_w - 160, fb_h - 45, "[ Desktop ]", COLOR_ACCENT_BLUE, 2);
}

static void render_desktop(uint32_t* fb, int fb_w, int fb_h) {
    // Wallpaper: Grid lines
    for (int y = 0; y < fb_h; y += 40) {
        draw_rect(fb, fb_w, fb_h, 0, y, fb_w, 1, 0xFF201815);
    }
    for (int x = 0; x < fb_w; x += 40) {
        draw_rect(fb, fb_w, fb_h, x, 0, 1, fb_h, 0xFF201815);
    }

    // Windows
    for (int i = 0; i < g_state.window_count; i++) {
        desktop_window_t* win = &g_state.windows[i];
        if (!win->is_visible) continue;

        // Window background
        draw_rect(fb, fb_w, fb_h, win->x, win->y, win->w, win->h, COLOR_SURFACE_DARK);
        draw_rect_outline(fb, fb_w, fb_h, win->x, win->y, win->w, win->h, COLOR_ACCENT_BLUE);

        // Title bar
        draw_rect(fb, fb_w, fb_h, win->x, win->y, win->w, 32, COLOR_PANEL_BG);
        draw_string(fb, fb_w, fb_h, win->x + 10, win->y + 8, win->title, COLOR_WHITE, 2);

        // Window controls [ _ ] [ + ] [ X ]
        draw_string(fb, fb_w, fb_h, win->x + win->w - 85, win->y + 8, "_  +  X", COLOR_ACCENT_AMBER, 2);

        // Window Content
        if (win->id == 0) { // Dialer
            draw_rect(fb, fb_w, fb_h, win->x + 20, win->y + 44, win->w - 40, 36, COLOR_BLACK);
            draw_string(fb, fb_w, fb_h, win->x + 30, win->y + 52, g_state.dialed_number, COLOR_WHITE, 2);

            static const char keys[4][3] = {
                {'1', '2', '3'},
                {'4', '5', '6'},
                {'7', '8', '9'},
                {'*', '0', '#'}
            };
            int bw = 65, bh = 45;
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 3; c++) {
                    int bx = win->x + 20 + c * (bw + 10);
                    int by = win->y + 90 + r * (bh + 8);
                    draw_rect(fb, fb_w, fb_h, bx, by, bw, bh, COLOR_PANEL_BG);
                    char ks[2] = {keys[r][c], '\0'};
                    draw_string(fb, fb_w, fb_h, bx + 26, by + 14, ks, COLOR_WHITE, 2);
                }
            }
            draw_rect(fb, fb_w, fb_h, win->x + 30, win->y + 315, win->w - 60, 40, COLOR_ACCENT_GREEN);
            draw_string(fb, fb_w, fb_h, win->x + win->w / 2 - 30, win->y + 325, "CALL", COLOR_WHITE, 2);
        } else if (win->id == 1) { // Terminal
            draw_rect(fb, fb_w, fb_h, win->x + 10, win->y + 40, win->w - 20, win->h - 50, COLOR_BLACK);
            for (int j = 0; j < g_state.term_history_count; j++) {
                draw_string(fb, fb_w, fb_h, win->x + 20, win->y + 50 + j * 24, g_state.term_history[j], COLOR_ACCENT_GREEN, 1);
            }
            draw_string(fb, fb_w, fb_h, win->x + 20, win->y + 50 + g_state.term_history_count * 24, "$ vm-subsystem running", COLOR_WHITE, 1);
        } else if (win->id == 2) { // System Monitor
            char info[64];
            snprintf(info, sizeof(info), "Host Battery: %d%% (%s)", g_state.battery_percent, g_state.is_charging ? "Charging" : "Discharging");
            draw_string(fb, fb_w, fb_h, win->x + 20, win->y + 50, info, COLOR_WHITE, 2);

            snprintf(info, sizeof(info), "Virtual Telephony: %s", g_state.is_call_ringing ? "RINGING" : "IDLE");
            draw_string(fb, fb_w, fb_h, win->x + 20, win->y + 80, info, g_state.is_call_ringing ? COLOR_ACCENT_GREEN : COLOR_TEXT_MUTED, 2);

            snprintf(info, sizeof(info), "Network: %s", g_state.is_network_connected ? "ONLINE (4G/WiFi)" : "DISCONNECTED");
            draw_string(fb, fb_w, fb_h, win->x + 20, win->y + 110, info, COLOR_ACCENT_BLUE, 2);

            snprintf(info, sizeof(info), "Kernel Ticks: %u", g_state.ticks);
            draw_string(fb, fb_w, fb_h, win->x + 20, win->y + 140, info, COLOR_TEXT_MUTED, 2);
        }
    }

    // Taskbar
    draw_rect(fb, fb_w, fb_h, 0, fb_h - 48, fb_w, 48, COLOR_SURFACE_DARK);
    draw_rect(fb, fb_w, fb_h, 6, fb_h - 42, 130, 36, COLOR_ACCENT_BLUE);
    draw_string(fb, fb_w, fb_h, 16, fb_h - 32, "SplitOS [M]", COLOR_WHITE, 2);

    // Open Window Tabs in Taskbar
    int tab_x = 150;
    for (int i = 0; i < g_state.window_count; i++) {
        desktop_window_t* win = &g_state.windows[i];
        if (!win->is_visible) continue;
        draw_rect(fb, fb_w, fb_h, tab_x, fb_h - 42, 140, 36, (g_state.focused_window_id == i) ? COLOR_PANEL_BG : COLOR_BG_DARK);
        draw_string(fb, fb_w, fb_h, tab_x + 10, fb_h - 32, win->title, COLOR_TEXT_PRIMARY, 1);
        tab_x += 148;
    }

    // System Tray
    char tray_str[32];
    snprintf(tray_str, sizeof(tray_str), "%d%% | 12:00", g_state.battery_percent);
    draw_string(fb, fb_w, fb_h, fb_w - 140, fb_h - 32, tray_str, COLOR_TEXT_PRIMARY, 2);

    // Mouse Pointer
    draw_mouse_cursor(fb, fb_w, fb_h, g_state.pointer_x, g_state.pointer_y);
}

void compositor_render(uint32_t* fb, int width, int height) {
    if (!fb || width <= 0 || height <= 0) return;

    // Clear background
    draw_rect(fb, width, height, 0, 0, width, height, COLOR_BG_DARK);

    if (g_state.mode == MODE_MOBILE) {
        render_status_bar(fb, width, height);
        render_incoming_call_banner(fb, width, height);

        switch (g_state.active_mobile_app) {
            case APP_DIALER:
                render_mobile_dialer(fb, width, height);
                break;
            case APP_MESSAGES:
                render_mobile_messages(fb, width, height);
                break;
            case APP_TERMINAL:
                render_mobile_terminal(fb, width, height);
                break;
            default:
                render_mobile_home(fb, width, height);
                break;
        }

        render_mobile_nav_bar(fb, width, height);
    } else {
        render_desktop(fb, width, height);
    }
}

size_t compositor_get_snapshot_size(void) {
    return sizeof(compositor_state_t);
}

void compositor_save_snapshot(void* dest) {
    if (dest) {
        memcpy(dest, &g_state, sizeof(compositor_state_t));
    }
}

void compositor_restore_snapshot(const void* src) {
    if (src) {
        memcpy(&g_state, src, sizeof(compositor_state_t));
    }
}
