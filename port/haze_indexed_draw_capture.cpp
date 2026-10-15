#include "haze_indexed_draw_capture.h"
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

extern "C" {
extern std::uint8_t* vm_base;
int ppu_native_memory_range(std::uint32_t, std::uint32_t);
}

namespace haze_indexed_capture {
namespace {
#ifndef HAZE_INDEXED_CAPTURE_CAPACITY
constexpr unsigned capacity = 512;
#else
constexpr unsigned capacity = HAZE_INDEXED_CAPTURE_CAPACITY;
#endif
constexpr unsigned max_payload = 4096;
static_assert(capacity > 0 && capacity <= 16384);
enum : unsigned { bad_input=1, refill=2, changed_context=4,
                  bad_payload=8, unfinished=16, reentered=32 };
struct record {
    std::uint64_t qpc_begin, qpc_end;
    std::uint32_t id, site, lr, context, begin, end, cursor, final_cursor;
    std::uint32_t offset, type, primitive, count, alignment, flags;
    std::uint32_t callbacks_before, callbacks_after, last_callback, last_words;
    std::uint32_t payload_bytes, reserved;
    std::uint8_t payload[max_payload];
};
static_assert(sizeof(record) == 96 + max_payload);
struct header {
    char magic[8];
    std::uint32_t version, record_size, count, payload_capacity;
    std::uint64_t qpc_frequency;
};
static_assert(sizeof(header) == 32);
record records[capacity]{};
unsigned used = 0, completed = 0;
bool written = false;
std::uint64_t qpc() noexcept {
    LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return t.QuadPart;
}
std::uint32_t read32(std::uint32_t a) noexcept {
    const auto* p=vm_base+a;
    return std::uint32_t(p[0])<<24 | std::uint32_t(p[1])<<16 |
           std::uint32_t(p[2])<<8 | p[3];
}
struct configuration {
    const char* output = std::getenv("HAZE_INDEXED_CAPTURE_OUT");
    const char* gate = std::getenv("HAZE_INDEXED_CAPTURE_GATE");
    ULONGLONG next_check=0;
    bool armed=false;
    bool enabled() noexcept {
        if (!output || !*output || !gate || !*gate || used==capacity) return false;
        if (armed) return true;
        const auto now=GetTickCount64();
        if(now<next_check) return false;
        next_check=now+250;
        const DWORD attributes=GetFileAttributesA(gate);
        armed=attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
        return armed;
    }
};
configuration& config() { static configuration c; return c; }
void write_capture() noexcept {
    if(written || used!=capacity || completed!=used) return;
    written=true;
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    header h{{'H','Z','I','D','R','A','W','1'},1,sizeof(record),used,max_payload,
             static_cast<std::uint64_t>(frequency.QuadPart)};
    HANDLE file=CreateFileA(config().output,GENERIC_WRITE,0,nullptr,CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) {
        std::fprintf(stderr,"[indexed-capture] create failed error=%lu\n",GetLastError()); return;
    }
    DWORD count=0;
    bool ok=WriteFile(file,&h,sizeof h,&count,nullptr) && count==sizeof h;
    const DWORD bytes=sizeof(records);
    ok=ok && WriteFile(file,records,bytes,&count,nullptr) && count==bytes;
    CloseHandle(file);
    std::fprintf(stderr,"[indexed-capture] records=%u complete=%u write_ok=%d\n",used,completed,int(ok));
}
}

scope::scope(std::uint32_t thread,std::uint64_t toc,std::uint64_t lr) noexcept
    :eligible_(thread==1 && toc==0xE94EE0 && lr<=UINT32_MAX),lr_(std::uint32_t(lr)) {}
scope::~scope() noexcept {
    if(slot_>=0) {records[slot_].flags|=unfinished;finish();}
}
void scope::callback(std::uint64_t target,std::uint64_t words) noexcept {
    if(!eligible_) return;
    ++callbacks_;last_target_=std::uint32_t(target);last_words_=std::uint32_t(words);
    if(slot_>=0) records[slot_].flags|=refill;
}
void scope::begin(std::uint32_t site,std::uint64_t context,std::uint64_t cursor,
                  std::uint64_t offset,std::uint64_t type,std::uint64_t primitive,
                  std::uint64_t count,std::uint64_t alignment) noexcept {
    if(!eligible_) return;
    if(slot_>=0) {records[slot_].flags|=reentered;return;}
    if(!config().enabled()) return;
    slot_=int(used++);
    auto& r=records[slot_];
    r.id=slot_+1;r.site=site;r.lr=lr_;r.qpc_begin=qpc();
    r.context=std::uint32_t(context);r.cursor=std::uint32_t(cursor);
    r.offset=std::uint32_t(offset);r.type=std::uint32_t(type);
    r.primitive=std::uint32_t(primitive);r.count=std::uint32_t(count);
    r.alignment=std::uint32_t(alignment);r.callbacks_before=callbacks_;
    if((site!=0x731390 && site!=0x73167C) || context>UINT32_MAX || cursor>UINT32_MAX ||
       offset>UINT32_MAX || type>UINT32_MAX || primitive>UINT32_MAX || count>UINT32_MAX ||
       alignment>UINT32_MAX || (context&3) || (cursor&3) ||
       !ppu_native_memory_range(r.context,16)) {r.flags|=bad_input;return;}
    r.begin=read32(r.context);r.end=read32(r.context+4);
    if(read32(r.context+8)!=r.cursor || r.begin>r.cursor || r.cursor>r.end)
        r.flags|=bad_input;
}
void scope::finish() noexcept {
    if(slot_<0) return;
    auto& r=records[slot_];
    r.qpc_end=qpc();r.callbacks_after=callbacks_-r.callbacks_before;
    r.last_callback=last_target_;r.last_words=last_words_;
    if(!(r.flags&bad_input)) {
        r.final_cursor=read32(r.context+8);
        if(read32(r.context)!=r.begin || read32(r.context+4)!=r.end)
            r.flags|=changed_context;
        if(r.final_cursor<r.cursor || r.final_cursor>r.end || (r.final_cursor&3) ||
           r.final_cursor-r.cursor>max_payload || r.final_cursor==r.cursor)
            r.flags|=bad_payload;
        if(!r.flags) {
            const auto bytes=r.final_cursor-r.cursor;
            if(!ppu_native_memory_range(r.cursor,bytes)) r.flags|=bad_payload;
            else {r.payload_bytes=bytes;std::memcpy(r.payload,vm_base+r.cursor,bytes);}
        }
    }
    ++completed;slot_=-1;write_capture();
}
}
