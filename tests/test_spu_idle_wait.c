/* Verify the actual Haze idle patch does not consume work or mutate SPU state,
 * and closes publication-before-sleep and freed-mapping races. */
#include <assert.h>
#include <windows.h>
#include "spu_coherency.h"
static unsigned before_waits;
static int publish_before_wait;
static void before_wait(void);
#define HAZE_IDLE_BEFORE_WAIT() before_wait()
#include "../port/haze_spu_idle.c"
unsigned char* vm_base;
void spu_ch_wake(spu_context* ctx) { (void)ctx; }
static spu_context current, original;

static void write_be32(unsigned char* p, uint32_t v)
{ p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void before_wait(void)
{
    ++before_waits;
    if (publish_before_wait) {
        spu_lockline_lock();
        write_be32(vm_base + 0x104, 1);
        spu_coh_notify_write(0x100);
        spu_lockline_unlock();
    }
}
static void reset(void)
{
    memset(&current, 0x3A, sizeof(current));
    current.ls = current.ls_store;
    current.image_id = 29; current.pc = 0x33A4;
    current.gpr[3]._u32[0] = 0x80410901;
    current.gpr[83]._u32[0] = 0x200;
    current.gpr[82]._u32[0] = 0x204;
    current.gpr[84]._u32[0] = 4;
    current.int_enable = 0;
    write_be32(current.ls + 0x204, 0x100);
    memset(vm_base, 0, 4096);
    write_be32(vm_base + 0x108, 16);
    write_be32(vm_base + 0x10C, 8);
    write_be32(vm_base + 0x11C, 2);
    before_waits = 0; publish_before_wait = 0;
}
static void unchanged_skip(void)
{
    unsigned char memory[4096]; memcpy(memory, vm_base, sizeof(memory));
    memcpy(&original, &current, sizeof(current));
    assert(!idle_wait_empty_queue(&current));
    assert(!memcmp(&current, &original, sizeof(current)));
    assert(!memcmp(memory, vm_base, sizeof(memory)) && !before_waits);
}
int main(void)
{
    vm_base = VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(vm_base);
    reset(); _putenv("HAZE_SPU_IDLE_WAIT="); _putenv("HAZE_SPU_IDLE_CONTROL=");
    haze_spu_idle_wait(&current); assert(!before_waits); /* Default off. */
    reset(); current.image_id = 28; unchanged_skip();
    reset(); current.pc = 0x33D0; unchanged_skip();
    reset(); current.gpr[3]._u32[0] = 0; unchanged_skip();
    reset(); current.gpr[3]._u32[0] = 0x8041090A; unchanged_skip();
    reset(); current.int_enable = 1; current.event_mask = current.event_status = 0x400; unchanged_skip();
    reset(); current.gpr[83]._u32[0] = SPU_LS_SIZE; unchanged_skip();
    reset(); current.gpr[82]._u32[0] = 0x208; unchanged_skip();
    reset(); current.gpr[84]._u32[0] = 0; unchanged_skip();
    reset(); current.ls = NULL; unchanged_skip();
    reset(); write_be32(current.ls + 0x204, 0); unchanged_skip();
    reset(); write_be32(current.ls + 0x204, 0x101); unchanged_skip();
    reset(); write_be32(vm_base + 0x104, 1); unchanged_skip(); /* Already ready. */
    reset(); write_be32(vm_base + 0x100, ~0u); write_be32(vm_base + 0x104, ~0u); unchanged_skip();
    reset(); write_be32(vm_base + 0x10C, 0); unchanged_skip();
    reset(); write_be32(vm_base + 0x108, 15); unchanged_skip();
    reset(); write_be32(vm_base + 0x11C, 1); unchanged_skip();
    reset(); write_be32(vm_base + 0x100, 16); write_be32(vm_base + 0x104, 16); unchanged_skip();
    reset();
    spu_lockline_lock();
    int collision = spu_coh_watch_queue(0x20180, vm_base + 0x100);
    assert(collision >= 0);
    spu_lockline_unlock();
    unchanged_skip(); /* Hash collision declines parking rather than losing a wake. */
    spu_lockline_lock(); spu_coh_unwatch_queue(collision); spu_lockline_unlock();
    reset(); memcpy(&original, &current, sizeof(current));
    unsigned char memory[4096]; memcpy(memory, vm_base, sizeof(memory));
    assert(idle_wait_empty_queue(&current) && before_waits == 1); /* Timeout. */
    assert(!memcmp(&current, &original, sizeof(current)));
    assert(!memcmp(memory, vm_base, sizeof(memory))); /* No result invented. */
    reset(); memcpy(&original, &current, sizeof(current)); publish_before_wait = 1;
    assert(idle_wait_empty_queue(&current)); /* Producer won the unlock/sleep race. */
    assert(read_be32(vm_base + 0x100) == 0 && read_be32(vm_base + 0x104) == 1);
    assert(!memcmp(&current, &original, sizeof(current)));
    before_waits = 0; unchanged_skip(); /* Original pop must consume the item. */
    DWORD old; assert(VirtualProtect(vm_base,4096,PAGE_NOACCESS,&old));
    spu_lockline_lock(); spu_coh_notify_write_except(0x100,NULL); spu_lockline_unlock();
    assert(VirtualFree(vm_base,0,MEM_RELEASE));
    puts("Haze SPU idle wait: guards, unchanged architectural state, real publication race, no consumed work, bounded fallback and freed mapping passed");
    return 0;
}
