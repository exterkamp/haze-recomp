#pragma once

// Included at the audited caller in the generated TU, after its trampoline
// macro and function declarations. Keep the original memory accessors and
// shader callbacks: this removes register-machine loop bookkeeping and guest
// spills, without bypassing memory synchronization or caching mutable state.
static bool haze_native_render_parameters(ppu_context* ctx) {
    if (ctx->thread_id != 1 || ctx->gpr[2] != 0xE94EE0 ||
        ctx->gpr[3] > UINT32_MAX || ctx->gpr[1] < 0xC0 ||
        ctx->gpr[1] > UINT32_MAX - 0x10 || (ctx->gpr[1] & 15)) return false;

    // The guard restricts this path to the single guest main thread.
    static bool reported = false;
    if (!reported) {
        fprintf(stderr, "[haze-native-render-parameters] enabled=1 guest_thread=1 caller=006741AC\n");
        reported = true;
    }

    const uint32_t record = uint32_t(ctx->gpr[3]);
    const bool force = uint8_t(ctx->gpr[5]) != 0;
    const uint64_t stack = ctx->gpr[1], link = ctx->lr;
    const uint32_t cr2 = ctx->cr & 0x0000F000u;
    // Retain the callee stack depth and backchain for nested guest calls. The
    // saved nonvolatile registers live in native locals instead of guest RAM.
    ctx->gpr[1] -= 0xC0;
    vm_write64(ctx->gpr[1], stack);
    if (record) {
        const uint32_t metadata = record - 32;
        const uint32_t vertex_program = vm_read32(record + 8);
        if (vertex_program) {
            const unsigned count = vm_read16(metadata + 6);
            const uint32_t entries = vm_read32(metadata);
            for (unsigned i = 0; i < count; ++i) {
                const uint32_t entry = vm_read32(entries + i * 4);
                if (!(vm_read8(entry + 0x1B) & 2)) continue;
                ctx->gpr[3] = vertex_program;
                ctx->gpr[4] = entry;
                ctx->lr = 0x006737FC;
                func_0072E398(ctx); DRAIN_TRAMPOLINE(ctx);
            }
        }
        const uint32_t fragment_program = vm_read32(record + 12);
        if (fragment_program) {
            const uint32_t values = vm_read32(record + 28);
            const unsigned count = vm_read16(metadata + 14);
            const uint32_t entries = vm_read32(metadata + 8);
            bool updated = false;
            for (unsigned i = 0; i < count; ++i) {
                const uint32_t slot = entries + i * 4;
                if (!force && !(vm_read8(vm_read32(slot) + 0x1B) & 1)) continue;
                ctx->gpr[3] = fragment_program;
                // Reload after inspecting flags, as the original does.
                ctx->gpr[4] = vm_read32(slot);
                ctx->gpr[5] = vm_read32(values + i * 4);
                ctx->lr = 0x0067387C;
                func_0072A738(ctx); DRAIN_TRAMPOLINE(ctx);
                updated = true;
            }
            if (updated) {
                ctx->gpr[3] = fragment_program;
                ctx->lr = 0x006738F8;
                func_007286A0(ctx); DRAIN_TRAMPOLINE(ctx);
            }
            // Callbacks can change the metadata; do not reuse earlier counts
            // or table addresses across this boundary.
            const unsigned sampler_count = vm_read16(metadata + 12);
            const unsigned parameter_count = vm_read16(metadata + 14);
            const uint32_t samplers = vm_read32(metadata + 8) + parameter_count * 4;
            for (unsigned i = 0; i < sampler_count; ++i) {
                const uint32_t slot = samplers + i * 4;
                if (!force && !(vm_read8(vm_read32(slot) + 0x1B) & 1)) continue;
                const uint32_t units = vm_read32(record + 24);
                const uint32_t entry = vm_read32(slot);
                const uint32_t unit = vm_read32(units + i * 4);
                if (unit <= 15) {
                    ctx->gpr[3] = vm_read32(entry);
                    ctx->gpr[4] = unit;
                    ctx->lr = 0x006739C8;
                    func_00725228(ctx); DRAIN_TRAMPOLINE(ctx);
                }
                vm_write8(entry + 0x1B, vm_read8(entry + 0x1B) & ~1u);
            }
        }
    }
    ctx->cr = (ctx->cr & ~0x0000F000u) | cr2;
    ctx->gpr[1] = stack;
    ctx->lr = link;
    return true;
}
