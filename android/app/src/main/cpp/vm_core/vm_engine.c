#include "vm_engine.h"
#include "../display/display_bridge.h"
#include "../serial/uart_ipc.h"
#include "../os_compositor.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "VmEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) printf("[VmEngine INFO] " __VA_ARGS__); printf("\n")
#define LOGE(...) fprintf(stderr, "[VmEngine ERROR] " __VA_ARGS__); fprintf(stderr, "\n")
#endif

static vm_engine_t g_vm = {
    .ram = NULL,
    .ram_size = GUEST_RAM_SIZE,
    .state = VM_STATE_STOPPED,
    .display_mode = MODE_MOBILE,
    .cpu_pc = 0,
    .total_instructions = 0
};

// Snapshot storage
static uint8_t* g_snapshot_ram = NULL;
static void* g_snapshot_compositor = NULL;
static size_t g_snapshot_compositor_size = 0;

static pthread_t g_vm_thread;
static pthread_mutex_t g_vm_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_vm_cond = PTHREAD_COND_INITIALIZER;
static int g_thread_running = 0;

static void* vm_thread_func(void* arg) {
    (void)arg;
    LOGI("VM execution worker thread started.");

    while (1) {
        pthread_mutex_lock(&g_vm_mutex);

        if (!g_thread_running) {
            pthread_mutex_unlock(&g_vm_mutex);
            break;
        }

        while (g_vm.state == VM_STATE_PAUSED && g_thread_running) {
            pthread_cond_wait(&g_vm_cond, &g_vm_mutex);
        }

        if (!g_thread_running || g_vm.state == VM_STATE_STOPPED) {
            pthread_mutex_unlock(&g_vm_mutex);
            break;
        }

        g_vm.total_instructions += 1000;
        pthread_mutex_unlock(&g_vm_mutex);

        // 1. Step guest compositor and check UART messages
        compositor_tick();

        // 2. Render frame to linear framebuffer
        int fb_w = 0, fb_h = 0;
        uint32_t* fb = display_bridge_get_fb(&fb_w, &fb_h);
        if (fb && fb_w > 0 && fb_h > 0) {
            display_bridge_lock_fb();
            compositor_render(fb, fb_w, fb_h);
            display_bridge_unlock_fb();

            // 3. Blit to ANativeWindow
            display_bridge_render_frame();
        }

        // Target ~60 FPS (16.6 ms per frame)
        usleep(16600);
    }

    LOGI("VM execution worker thread terminated.");
    return NULL;
}

int vm_engine_init(const char* guest_img_path) {
    pthread_mutex_lock(&g_vm_mutex);

    if (g_vm.ram == NULL) {
        g_vm.ram = (uint8_t*)malloc(GUEST_RAM_SIZE);
        if (!g_vm.ram) {
            LOGE("Failed to allocate %zu bytes for guest RAM", (size_t)GUEST_RAM_SIZE);
            pthread_mutex_unlock(&g_vm_mutex);
            return -1;
        }
        memset(g_vm.ram, 0, GUEST_RAM_SIZE);
    }

    if (guest_img_path && strlen(guest_img_path) > 0) {
        strncpy(g_vm.guest_image_path, guest_img_path, sizeof(g_vm.guest_image_path) - 1);
        FILE* f = fopen(guest_img_path, "rb");
        if (f) {
            size_t bytes_read = fread(g_vm.ram, 1, GUEST_RAM_SIZE, f);
            fclose(f);
            LOGI("Loaded %zu bytes from guest OS image: %s", bytes_read, guest_img_path);
        } else {
            LOGI("Guest OS image file not found or inaccessible; initializing default firmware.");
        }
    }

    // Allocate snapshot buffers
    if (!g_snapshot_ram) {
        g_snapshot_ram = (uint8_t*)malloc(GUEST_RAM_SIZE);
    }
    g_snapshot_compositor_size = compositor_get_snapshot_size();
    if (!g_snapshot_compositor) {
        g_snapshot_compositor = malloc(g_snapshot_compositor_size);
    }

    display_bridge_init(DEFAULT_FB_WIDTH, DEFAULT_FB_HEIGHT);
    compositor_init(g_vm.display_mode);

    g_vm.state = VM_STATE_STOPPED;
    g_vm.cpu_pc = 0x80000000;
    g_vm.total_instructions = 0;

    pthread_mutex_unlock(&g_vm_mutex);
    LOGI("VM Engine initialized successfully.");
    return 0;
}

void vm_engine_cleanup(void) {
    vm_engine_stop();

    pthread_mutex_lock(&g_vm_mutex);
    if (g_vm.ram) {
        free(g_vm.ram);
        g_vm.ram = NULL;
    }
    if (g_snapshot_ram) {
        free(g_snapshot_ram);
        g_snapshot_ram = NULL;
    }
    if (g_snapshot_compositor) {
        free(g_snapshot_compositor);
        g_snapshot_compositor = NULL;
    }

    compositor_cleanup();
    display_bridge_cleanup();
    pthread_mutex_unlock(&g_vm_mutex);

    LOGI("VM Engine cleaned up.");
}

int vm_engine_start(void) {
    pthread_mutex_lock(&g_vm_mutex);
    if (g_vm.state == VM_STATE_RUNNING) {
        pthread_mutex_unlock(&g_vm_mutex);
        return 1;
    }

    if (!g_vm.ram) {
        pthread_mutex_unlock(&g_vm_mutex);
        if (vm_engine_init(g_vm.guest_image_path) != 0) {
            return 0;
        }
        pthread_mutex_lock(&g_vm_mutex);
    }

    g_vm.state = VM_STATE_RUNNING;
    g_thread_running = 1;

    if (pthread_create(&g_vm_thread, NULL, vm_thread_func, NULL) != 0) {
        LOGE("Failed to spawn VM execution pthread");
        g_vm.state = VM_STATE_STOPPED;
        g_thread_running = 0;
        pthread_mutex_unlock(&g_vm_mutex);
        return 0;
    }

    pthread_mutex_unlock(&g_vm_mutex);
    LOGI("VM Engine started successfully.");
    return 1;
}

int vm_engine_stop(void) {
    pthread_mutex_lock(&g_vm_mutex);
    if (g_vm.state == VM_STATE_STOPPED && !g_thread_running) {
        pthread_mutex_unlock(&g_vm_mutex);
        return 1;
    }

    g_vm.state = VM_STATE_STOPPED;
    g_thread_running = 0;
    pthread_cond_broadcast(&g_vm_cond);
    pthread_mutex_unlock(&g_vm_mutex);

    pthread_join(g_vm_thread, NULL);
    LOGI("VM Engine stopped.");
    return 1;
}

int vm_engine_pause(void) {
    pthread_mutex_lock(&g_vm_mutex);
    if (g_vm.state != VM_STATE_RUNNING) {
        pthread_mutex_unlock(&g_vm_mutex);
        return 0;
    }

    g_vm.state = VM_STATE_PAUSED;

    // Capture instantaneous RAM & Compositor snapshot
    if (g_vm.ram && g_snapshot_ram) {
        memcpy(g_snapshot_ram, g_vm.ram, GUEST_RAM_SIZE);
    }
    if (g_snapshot_compositor) {
        compositor_save_snapshot(g_snapshot_compositor);
    }

    pthread_mutex_unlock(&g_vm_mutex);
    LOGI("VM execution state paused and snapshot preserved.");
    return 1;
}

int vm_engine_resume(void) {
    pthread_mutex_lock(&g_vm_mutex);
    if (g_vm.state != VM_STATE_PAUSED) {
        pthread_mutex_unlock(&g_vm_mutex);
        return 0;
    }

    // Restore RAM & Compositor snapshot
    if (g_vm.ram && g_snapshot_ram) {
        memcpy(g_vm.ram, g_snapshot_ram, GUEST_RAM_SIZE);
    }
    if (g_snapshot_compositor) {
        compositor_restore_snapshot(g_snapshot_compositor);
    }

    g_vm.state = VM_STATE_RUNNING;
    pthread_cond_broadcast(&g_vm_cond);
    pthread_mutex_unlock(&g_vm_mutex);

    LOGI("VM execution resumed from snapshot.");
    return 1;
}

void vm_engine_send_touch(float x, float y, int action) {
    compositor_handle_touch(x, y, action);
}

void vm_engine_set_display_mode(int mode) {
    pthread_mutex_lock(&g_vm_mutex);
    g_vm.display_mode = mode;
    compositor_set_mode(mode);
    pthread_mutex_unlock(&g_vm_mutex);
}

int vm_engine_get_state(void) {
    pthread_mutex_lock(&g_vm_mutex);
    int s = g_vm.state;
    pthread_mutex_unlock(&g_vm_mutex);
    return s;
}

uint64_t vm_engine_get_ram_usage(void) {
    pthread_mutex_lock(&g_vm_mutex);
    uint64_t usage = (uint64_t)GUEST_RAM_SIZE;
    int fb_w = 0, fb_h = 0;
    display_bridge_get_fb(&fb_w, &fb_h);
    usage += (uint64_t)(fb_w * fb_h * sizeof(uint32_t));
    pthread_mutex_unlock(&g_vm_mutex);
    return usage;
}
