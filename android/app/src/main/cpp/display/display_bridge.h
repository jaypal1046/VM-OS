#ifndef DISPLAY_BRIDGE_H
#define DISPLAY_BRIDGE_H

#include <stdint.h>
#include <pthread.h>

#ifdef __ANDROID__
#include <android/native_window.h>
#include <android/native_window_jni.h>
#else
// For non-Android host testing / native unit tests
typedef void ANativeWindow;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_FB_WIDTH  720
#define DEFAULT_FB_HEIGHT 1280

typedef struct {
    uint32_t* buffer;
    int width;
    int height;
    int stride;
    pthread_mutex_t lock;
    ANativeWindow* window;
    int has_window;
} display_bridge_t;

int display_bridge_init(int width, int height);
void display_bridge_cleanup(void);
void display_bridge_set_window(ANativeWindow* window);
void display_bridge_resize(int new_width, int new_height);
void display_bridge_render_frame(void);

uint32_t* display_bridge_get_fb(int* out_width, int* out_height);
void display_bridge_lock_fb(void);
void display_bridge_unlock_fb(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_BRIDGE_H
