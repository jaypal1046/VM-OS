#include "display_bridge.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "DisplayBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) printf("[DisplayBridge INFO] " __VA_ARGS__); printf("\n")
#define LOGE(...) fprintf(stderr, "[DisplayBridge ERROR] " __VA_ARGS__); fprintf(stderr, "\n")
#endif

static display_bridge_t g_display = {
    .buffer = NULL,
    .width = 0,
    .height = 0,
    .stride = 0,
    .window = NULL,
    .has_window = 0
};

static pthread_mutex_t g_display_mutex = PTHREAD_MUTEX_INITIALIZER;

int display_bridge_init(int width, int height) {
    pthread_mutex_lock(&g_display_mutex);

    if (g_display.buffer) {
        free(g_display.buffer);
        g_display.buffer = NULL;
    }

    g_display.width = width > 0 ? width : DEFAULT_FB_WIDTH;
    g_display.height = height > 0 ? height : DEFAULT_FB_HEIGHT;
    g_display.stride = g_display.width;

    size_t buffer_size = (size_t)g_display.width * (size_t)g_display.height * sizeof(uint32_t);
    g_display.buffer = (uint32_t*)malloc(buffer_size);
    if (!g_display.buffer) {
        LOGE("Failed to allocate %zu bytes for framebuffer", buffer_size);
        pthread_mutex_unlock(&g_display_mutex);
        return -1;
    }

    // Initialize with dark background (#121418: RGBA 0x12, 0x14, 0x18, 0xFF -> 0xFF181412)
    for (int i = 0; i < g_display.width * g_display.height; i++) {
        g_display.buffer[i] = 0xFF181412;
    }

    LOGI("DisplayBridge initialized: %dx%d (32-bit RGBA)", g_display.width, g_display.height);
    pthread_mutex_unlock(&g_display_mutex);
    return 0;
}

void display_bridge_cleanup(void) {
    pthread_mutex_lock(&g_display_mutex);
    if (g_display.buffer) {
        free(g_display.buffer);
        g_display.buffer = NULL;
    }
#ifdef __ANDROID__
    if (g_display.window) {
        ANativeWindow_release(g_display.window);
        g_display.window = NULL;
    }
#else
    g_display.window = NULL;
#endif
    g_display.has_window = 0;
    g_display.width = 0;
    g_display.height = 0;
    pthread_mutex_unlock(&g_display_mutex);
    LOGI("DisplayBridge cleaned up.");
}

void display_bridge_set_window(ANativeWindow* window) {
    pthread_mutex_lock(&g_display_mutex);

#ifdef __ANDROID__
    if (g_display.window) {
        ANativeWindow_release(g_display.window);
        g_display.window = NULL;
        g_display.has_window = 0;
    }

    if (window) {
        g_display.window = window;
        ANativeWindow_acquire(window);
        ANativeWindow_setBuffersGeometry(window, g_display.width, g_display.height, WINDOW_FORMAT_RGBA_8888);
        g_display.has_window = 1;
        LOGI("NativeWindow acquired: buffers geometry set to %dx%d", g_display.width, g_display.height);
    }
#else
    g_display.window = window;
    g_display.has_window = (window != NULL);
#endif

    pthread_mutex_unlock(&g_display_mutex);
}

void display_bridge_resize(int new_width, int new_height) {
    if (new_width <= 0 || new_height <= 0) return;

    pthread_mutex_lock(&g_display_mutex);
    if (g_display.width == new_width && g_display.height == new_height) {
        pthread_mutex_unlock(&g_display_mutex);
        return;
    }

    uint32_t* old_buf = g_display.buffer;
    size_t new_size = (size_t)new_width * (size_t)new_height * sizeof(uint32_t);
    uint32_t* new_buf = (uint32_t*)malloc(new_size);

    if (new_buf) {
        for (int i = 0; i < new_width * new_height; i++) {
            new_buf[i] = 0xFF181412;
        }
        g_display.buffer = new_buf;
        g_display.width = new_width;
        g_display.height = new_height;
        g_display.stride = new_width;
        if (old_buf) free(old_buf);

#ifdef __ANDROID__
        if (g_display.window) {
            ANativeWindow_setBuffersGeometry(g_display.window, new_width, new_height, WINDOW_FORMAT_RGBA_8888);
        }
#endif
        LOGI("Framebuffer resized to: %dx%d", new_width, new_height);
    }
    pthread_mutex_unlock(&g_display_mutex);
}

void display_bridge_render_frame(void) {
    pthread_mutex_lock(&g_display_mutex);

#ifdef __ANDROID__
    if (!g_display.has_window || !g_display.window || !g_display.buffer) {
        pthread_mutex_unlock(&g_display_mutex);
        return;
    }

    ANativeWindow_Buffer out_buffer;
    if (ANativeWindow_lock(g_display.window, &out_buffer, NULL) < 0) {
        pthread_mutex_unlock(&g_display_mutex);
        return;
    }

    int copy_w = g_display.width < out_buffer.width ? g_display.width : out_buffer.width;
    int copy_h = g_display.height < out_buffer.height ? g_display.height : out_buffer.height;

    uint32_t* src = g_display.buffer;
    uint32_t* dst = (uint32_t*)out_buffer.bits;

    for (int y = 0; y < copy_h; y++) {
        memcpy(dst + (y * out_buffer.stride), src + (y * g_display.stride), copy_w * sizeof(uint32_t));
    }

    ANativeWindow_unlockAndPost(g_display.window);
#endif

    pthread_mutex_unlock(&g_display_mutex);
}

uint32_t* display_bridge_get_fb(int* out_width, int* out_height) {
    if (out_width) *out_width = g_display.width;
    if (out_height) *out_height = g_display.height;
    return g_display.buffer;
}

void display_bridge_lock_fb(void) {
    pthread_mutex_lock(&g_display_mutex);
}

void display_bridge_unlock_fb(void) {
    pthread_mutex_unlock(&g_display_mutex);
}
