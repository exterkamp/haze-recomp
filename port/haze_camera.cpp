#include "ppu_recomp.h"
#include "haze_camera.h"
#include "haze_input.h"
#include "haze_mouse.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

extern "C" void ppu_register_function(uint64_t,void (*)(ppu_context*));
namespace {
float read_float(uint32_t address) {
    uint32_t bits=vm_read32(address); float value;
    std::memcpy(&value,&bits,sizeof(value)); return value;
}
void write_float(uint32_t address,float value) {
    uint32_t bits; std::memcpy(&bits,&value,sizeof(bits)); vm_write32(address,bits);
}
#ifdef _WIN32
// Diagnostic input for repeatable performance comparisons. Absolute angles
// avoid accumulating yaw drift when the two variants have different pad poll
// and simulation rates. This is used only at the already audited local-player
// mouse boundary; normal launches never read a control file or sample QPC here.
struct CameraBenchmark {
    const char* path=std::getenv("HAZE_BENCHMARK_CAMERA_FILE");
    FILE* csv=nullptr;
    LONGLONG frequency=0, next_poll=0, origin=0, sample_qpc=0;
    unsigned token=0, owner=0, samples=0;
    double anchor_yaw=0, anchor_pitch=0, wanted_yaw=0, wanted_pitch=0;
    bool failed=false;
    bool apply(uint32_t motor) {
        if (!path || !*path || failed) return false;
        LARGE_INTEGER clock;
        if (!frequency) { QueryPerformanceFrequency(&clock); frequency=clock.QuadPart; }
        QueryPerformanceCounter(&clock); sample_qpc=clock.QuadPart;
        if (sample_qpc>=next_poll) {
            next_poll=sample_qpc+frequency/10;
            unsigned requested=token;
            if (FILE* file=std::fopen(path,"r")) {
                unsigned value;
                if (std::fscanf(file,"%u",&value)==1 && value<=1000000) requested=value;
                std::fclose(file);
            }
            if (requested!=token) {
                // The game shutdown can bypass CRT stream cleanup. Preserve
                // the final partial batch when the recorder disables input.
                if (csv) std::fflush(csv);
                token=requested; origin=sample_qpc;
                std::fprintf(stderr,"[benchmark-camera-mode] token=%u qpc=%lld period_s=4 amplitude_deg=30\n",token,sample_qpc);
            }
        }
        if (!token) return false;
        const double yaw=read_float(motor+0xb0), base=read_float(motor+0xb4);
        const double pitch=read_float(motor+0xac);
        const float lower=read_float(motor+0x84), upper=read_float(motor+0x80);
        if (!std::isfinite(yaw) || !std::isfinite(base) || !std::isfinite(pitch) ||
            !std::isfinite(lower) || !std::isfinite(upper) || lower>upper ||
            (owner && owner!=motor)) {
            failed=true;
            std::fprintf(stderr,"[benchmark-camera-invalid] motor=%08X owner=%08X qpc=%lld\n",motor,owner,sample_qpc);
            return false;
        }
        if (!owner) {
            owner=motor; anchor_yaw=yaw+base; anchor_pitch=pitch;
            char output[4096];
            const int n=std::snprintf(output,sizeof output,"%s.csv",path);
            if (n>0 && n<sizeof output) csv=std::fopen(output,"wb");
            if (csv) std::fprintf(csv,"qpc,qpc_frequency,token,motor,phase_s,wanted_yaw,actual_yaw,wanted_pitch,actual_pitch\n");
            std::fprintf(stderr,"[benchmark-camera-anchor] motor=%08X yaw=%.9f pitch=%.9f qpc=%lld\n",owner,anchor_yaw,anchor_pitch,sample_qpc);
        }
        constexpr double pi=3.14159265358979323846;
        const double seconds=double(sample_qpc-origin)/frequency;
        wanted_yaw=anchor_yaw+(pi/6)*std::sin(seconds*(2*pi/4));
        wanted_pitch=std::clamp(anchor_pitch,double(lower),double(upper));
        write_float(motor+0xb0,static_cast<float>(std::remainder(wanted_yaw-base,2*pi)));
        write_float(motor+0xac,static_cast<float>(wanted_pitch));
        vm_write32(motor+0x14,0); vm_write32(motor+0x18,0);
        vm_write32(motor+0xc8,0); vm_write32(motor+0xcc,0);
        return true;
    }
    void audit(uint32_t motor) {
        if (!csv) return;
        std::fprintf(csv,"%lld,%lld,%u,%u,%.9f,%.9f,%.9f,%.9f,%.9f\n",
            sample_qpc,frequency,token,motor,double(sample_qpc-origin)/frequency,
            wanted_yaw,double(read_float(motor+0xb0))+read_float(motor+0xb4),
            wanted_pitch,double(read_float(motor+0xac)));
        if ((++samples&63u)==0) std::fflush(csv);
    }
};
#endif
void update_look(ppu_context* ctx) {
    static const bool trace=std::getenv("HAZE_MOUSE_TRACE")!=nullptr;
    bool moved=false;
    float before_yaw=0,before_pitch=0;
    double mouse_x=0,mouse_y=0;
    const uint32_t motor=static_cast<uint32_t>(ctx->gpr[3]);
    const uint32_t actor=vm_read32(motor+0x10);
    // Only a player with the local G5 controls component may consume the mouse.
    // AI and remote characters use this motor too.
    const uint32_t controls=actor ? vm_read32(actor+0x1a0) : 0;
#ifdef _WIN32
    static CameraBenchmark benchmark;
    bool benchmark_applied=false;
#endif
    if (controls && vm_read32(controls)==0x00dd9f78) {
        double x=0,y=0,speed=0; bool invert=false;
        if (haze_input_take_mouse(x,y,speed,invert) && vm_read8(motor+0xa0)) {
            const float yaw=read_float(motor+0xb0),pitch=read_float(motor+0xac);
            const float upper=read_float(motor+0x80),lower=read_float(motor+0x84);
            if (std::isfinite(yaw) && std::isfinite(pitch) &&
                std::isfinite(lower) && std::isfinite(upper) && lower<=upper) {
                const auto look=haze_mouse_look(yaw,pitch,x,y,speed,invert,lower,upper);
                moved=x!=0 || y!=0;
                before_yaw=yaw+read_float(motor+0xb4); before_pitch=pitch;
                mouse_x=x; mouse_y=y;
                write_float(motor+0xb0,look.yaw);
                write_float(motor+0xac,look.pitch);
                // Suppress stick input and its angular velocity history. WASD,
                // recoil, animation, collision and camera limits still run.
                vm_write32(motor+0x14,0); vm_write32(motor+0x18,0);
                vm_write32(motor+0xc8,0); vm_write32(motor+0xcc,0);
            }
        }
#ifdef _WIN32
        if (benchmark.path && *benchmark.path && vm_read8(motor+0xa0))
            benchmark_applied=benchmark.apply(motor);
#endif
    }
    func_0024D3A0(ctx);
#ifdef _WIN32
    if (benchmark_applied) benchmark.audit(motor);
#endif
    if (trace && moved)
        std::fprintf(stderr,"[mouse] counts=%.0f,%.0f yaw=%.8f->%.8f pitch=%.8f->%.8f\n",
            mouse_x,mouse_y,before_yaw,read_float(motor+0xb0)+read_float(motor+0xb4),
            before_pitch,read_float(motor+0xac));
}
}
void haze_camera_initialize() {
    // BLUS30094 01.00: virtual character-look update, BEFORE camera/aim matrices
    // are built. Direct calls to this function are absent in this title's lift.
    // Refuse a different layout instead of writing guessed object offsets.
    if (vm_read32(0x24d3a0)!=0x7d800026 ||
        vm_read32(0x24d3a4)!=0xf821fed1 ||
        vm_read32(0x24dd70)!=0xd09f00ac ||
        vm_read32(0xde0c14)!=0xe499f0 || vm_read32(0xe499f0)!=0x24d3a0) {
        std::fprintf(stderr,"[input] Native mouse camera hook unavailable: unsupported Haze layout\n");
        return;
    }
    ppu_register_function(0x24d3a0,update_look);
    std::fprintf(stderr,"[input] Native mouse camera hook installed (distance-based rotation)\n");
}
