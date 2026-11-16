#ifndef GUEST_COMPOSITOR_H
#define GUEST_COMPOSITOR_H

#include <stdint.h>

void guest_compositor_init(int mode);
void guest_compositor_draw_scene(uint32_t* fb, int width, int height);
void guest_compositor_on_input(float norm_x, float norm_y, int action);

#endif // GUEST_COMPOSITOR_H
