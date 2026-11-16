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
// Serialize initialization and teardown across the interval spent joining a worker.
static pthread_mutex_t g_lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER;
static int vm_engine_stop_locked(void);

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

        // The current runtime executes the host compositor, not guest instructions.

        // 1. Step guest compositor and check UART messages
        compositor_tick();
        g_vm.display_mode = compositor_get_mode();

        // 2. Render frame to linear framebuffer
        int fb_w = 0, fb_h = 0;
        display_bridge_lock_fb();
        uint32_t* fb = display_bridge_get_fb(&fb_w, &fb_h);
        if (fb && fb_w > 0 && fb_h > 0) {
            compositor_render(fb, fb_w, fb_h);
        }
        display_bridge_unlock_fb();
        pthread_mutex_unlock(&g_vm_mutex);

        // Present outside the VM lock so a slow surface does not block input.
        display_bridge_render_frame();

        // Target ~60 FPS (16.6 ms per frame)
        usleep(16600);
    }

    LOGI("VM execution worker thread terminated.");
    return NULL;
}

static int vm_engine_init_locked(const char* guest_img_path) {
    pthread_mutex_lock(&g_vm_mutex);

    if (g_thread_running) {
        pthread_mutex_unlock(&g_vm_mutex);
        return -1;
    }

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
        if (guest_img_path != g_vm.guest_image_path) {
            snprintf(g_vm.guest_image_path, sizeof(g_vm.guest_image_path), "%s", guest_img_path);
        }
        FILE* f = fopen(guest_img_path, "rb");
        if (!f) {
            LOGE("Guest image could not be opened");
            goto init_failed;
        }
        memset(g_vm.ram, 0, GUEST_RAM_SIZE);
        size_t bytes_read = fread(g_vm.ram, 1, GUEST_RAM_SIZE, f);
        int extra = fgetc(f);
        int valid = !ferror(f) && extra == EOF && bytes_read >= 32 &&
                    memcmp(g_vm.ram, "SPLITOS\0", 8) == 0;
        fclose(f);
        if (!valid) {
            LOGE("Invalid or oversized guest image");
            goto init_failed;
        }
        LOGI("Loaded %zu bytes from guest OS image: %s", bytes_read, guest_img_path);
    }

    // Allocate snapshot buffers
    if (!g_snapshot_ram) {
        g_snapshot_ram = (uint8_t*)malloc(GUEST_RAM_SIZE);
    }
    g_snapshot_compositor_size = compositor_get_snapshot_size();
    if (!g_snapshot_compositor) {
        g_snapshot_compositor = malloc(g_snapshot_compositor_size);
    }

    if (!g_snapshot_ram || !g_snapshot_compositor) {
        LOGE("Failed to allocate snapshot buffers");
        goto init_failed;
    }
    if (display_bridge_init(DEFAULT_FB_WIDTH, DEFAULT_FB_HEIGHT) != 0) {
        goto init_failed;
    }
    compositor_init(g_vm.display_mode);

    g_vm.state = VM_STATE_STOPPED;
    g_vm.cpu_pc = 0x80000000;
    g_vm.total_instructions = 0;

    pthread_mutex_unlock(&g_vm_mutex);
    LOGI("VM Engine initialized successfully.");
    return 0;

init_failed:
    free(g_vm.ram);
    g_vm.ram = NULL;
    free(g_snapshot_ram);
    g_snapshot_ram = NULL;
    free(g_snapshot_compositor);
    g_snapshot_compositor = NULL;
    display_bridge_cleanup();
    pthread_mutex_unlock(&g_vm_mutex);
    return -1;
}

int vm_engine_init(const char* guest_img_path) {
    pthread_mutex_lock(&g_lifecycle_mutex);
    int result = vm_engine_init_locked(guest_img_path);
    pthread_mutex_unlock(&g_lifecycle_mutex);
    return result;
}

void vm_engine_cleanup(void) {
    pthread_mutex_lock(&g_lifecycle_mutex);
    vm_engine_stop_locked();

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

    pthread_mutex_unlock(&g_lifecycle_mutex);
    LOGI("VM Engine cleaned up.");
}

int vm_engine_start(void) {
    int result = 0;
    pthread_mutex_lock(&g_lifecycle_mutex);
    pthread_mutex_lock(&g_vm_mutex);
    if (g_vm.state == VM_STATE_RUNNING) {
        result = 1;
        goto done;
    }
    if (g_vm.state == VM_STATE_PAUSED) goto done;

    if (!g_vm.ram) {
        pthread_mutex_unlock(&g_vm_mutex);
        int initialized = vm_engine_init_locked(g_vm.guest_image_path);
        pthread_mutex_lock(&g_vm_mutex);
        if (initialized != 0) goto done;
    }

    g_vm.state = VM_STATE_RUNNING;
    g_thread_running = 1;
    result = pthread_create(&g_vm_thread, NULL, vm_thread_func, NULL) == 0;
    if (!result) {
        LOGE("Failed to spawn VM execution pthread");
        g_vm.state = VM_STATE_STOPPED;
        g_thread_running = 0;
    }
done:
    pthread_mutex_unlock(&g_vm_mutex);
    pthread_mutex_unlock(&g_lifecycle_mutex);
    return result;
}

static int vm_engine_stop_locked(void) {
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

int vm_engine_stop(void) {
    pthread_mutex_lock(&g_lifecycle_mutex);
    int result = vm_engine_stop_locked();
    pthread_mutex_unlock(&g_lifecycle_mutex);
    return result;
}

int vm_engine_pause(void) {
    pthread_mutex_lock(&g_vm_mutex);
    if (g_vm.state != VM_STATE_RUNNING) {
        pthread_mutex_unlock(&g_vm_mutex);
        return 0;
    }

    g_vm.state = VM_STATE_PAUSED;

    // shortcut: snapshot stays in memory; add disk persistence for process-death recovery.
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

    int display_mode = compositor_get_mode();
    // Restore RAM & Compositor snapshot
    if (g_vm.ram && g_snapshot_ram) {
        memcpy(g_vm.ram, g_snapshot_ram, GUEST_RAM_SIZE);
    }
    if (g_snapshot_compositor) {
        compositor_restore_snapshot(g_snapshot_compositor);
    }
    compositor_set_mode(display_mode);
    g_vm.display_mode = display_mode;

    g_vm.state = VM_STATE_RUNNING;
    pthread_cond_broadcast(&g_vm_cond);
    pthread_mutex_unlock(&g_vm_mutex);

    LOGI("VM execution resumed from snapshot.");
    return 1;
}

void vm_engine_send_touch(float x, float y, int action) {
    pthread_mutex_lock(&g_vm_mutex);
    if (g_vm.state == VM_STATE_RUNNING) {
        compositor_handle_touch(x, y, action);
        g_vm.display_mode = compositor_get_mode();
    }
    pthread_mutex_unlock(&g_vm_mutex);
}

void vm_engine_set_display_mode(int mode) {
    if (mode != MODE_MOBILE && mode != MODE_DESKTOP) return;
    pthread_mutex_lock(&g_vm_mutex);
    g_vm.display_mode = mode;
    compositor_set_mode(mode);
    pthread_mutex_unlock(&g_vm_mutex);
}

int vm_engine_get_display_mode(void) {
    pthread_mutex_lock(&g_vm_mutex);
    int mode = compositor_get_mode();
    pthread_mutex_unlock(&g_vm_mutex);
    return mode;
}

int vm_engine_get_state(void) {
    pthread_mutex_lock(&g_vm_mutex);
    int s = g_vm.state;
    pthread_mutex_unlock(&g_vm_mutex);
    return s;
}

uint64_t vm_engine_get_ram_usage(void) {
    pthread_mutex_lock(&g_vm_mutex);
    uint64_t usage = sizeof(g_vm) + compositor_get_snapshot_size();
    if (g_vm.ram) usage += g_vm.ram_size;
    if (g_snapshot_ram) usage += GUEST_RAM_SIZE;
    if (g_snapshot_compositor) usage += g_snapshot_compositor_size;
    int fb_w = 0, fb_h = 0;
    display_bridge_lock_fb();
    if (display_bridge_get_fb(&fb_w, &fb_h)) {
        usage += (uint64_t)fb_w * (uint64_t)fb_h * sizeof(uint32_t);
    }
    display_bridge_unlock_fb();
    pthread_mutex_unlock(&g_vm_mutex);
    return usage;
}
