#include "compositor.h"
#include <string.h>

static int g_current_mode = 0;

void guest_compositor_init(int mode) {
    g_current_mode = mode;
}

void guest_compositor_draw_scene(uint32_t* fb, int width, int height) {
    if (!fb || width <= 0 || height <= 0) return;
    // Renders active guest desktop/mobile scene
}

void guest_compositor_on_input(float norm_x, float norm_y, int action) {
    (void)norm_x;
    (void)norm_y;
    (void)action;
}
