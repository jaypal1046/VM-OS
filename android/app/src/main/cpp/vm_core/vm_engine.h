#ifndef VM_ENGINE_H
#define VM_ENGINE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VM_STATE_STOPPED 0
#define VM_STATE_RUNNING 1
#define VM_STATE_PAUSED  2

#define GUEST_RAM_SIZE (16 * 1024 * 1024) // 16 MB guest RAM footprint (< 64MB constraint)

typedef struct {
    uint8_t* ram;
    size_t ram_size;
    int state;
    int display_mode;
    uint32_t cpu_pc;
    uint32_t cpu_registers[16];
    uint64_t total_instructions;
    char guest_image_path[512];
} vm_engine_t;

int vm_engine_init(const char* guest_img_path);
void vm_engine_cleanup(void);

int vm_engine_start(void);
int vm_engine_stop(void);
int vm_engine_pause(void);
int vm_engine_resume(void);

void vm_engine_send_touch(float x, float y, int action);
void vm_engine_set_display_mode(int mode);

int vm_engine_get_state(void);
uint64_t vm_engine_get_ram_usage(void);

#ifdef __cplusplus
}
#endif

#endif // VM_ENGINE_H
