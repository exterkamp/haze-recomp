#ifndef HAZE_JOB_TRACE_H
#define HAZE_JOB_TRACE_H

#include <cstdint>

enum class HazeJobTraceKind : unsigned {
    Submit = 1,
    Dispatch = 2,
    Kernel = 3,
    Wait = 4,
    Handler = 5,
};

#if defined(HAZE_JOB_TRACE_TEST_CAPACITY)
#define HAZE_JOB_TRACE_CAPACITY HAZE_JOB_TRACE_TEST_CAPACITY
#else
#define HAZE_JOB_TRACE_CAPACITY 131072u
#endif

#if defined(HAZE_JOB_TRACE) && defined(_WIN32)
class HazeJobTraceScope {
public:
    HazeJobTraceScope(unsigned kind, unsigned guest_thread,
        std::uint64_t toc, std::uint64_t caller,
        const std::uint64_t* args, unsigned arg_count) noexcept;
    ~HazeJobTraceScope() noexcept;

    HazeJobTraceScope(const HazeJobTraceScope&) = delete;
    HazeJobTraceScope& operator=(const HazeJobTraceScope&) = delete;

    void set_slot(std::uint32_t slot) noexcept;
    void set_dispatch(std::uint64_t code, std::uint64_t toc) noexcept {
        if (active_) {
            dispatch_code_ = code;
            dispatch_toc_ = toc;
        }
    }

private:
    friend void haze_job_trace_dump() noexcept;
    friend void haze_job_trace_record(HazeJobTraceScope&, std::int64_t) noexcept;
    HazeJobTraceScope* previous_;
    bool active_;
    bool submit_generation_assigned_;
    unsigned kind_, guest_thread_, arg_count_, depth_;
    std::uint32_t host_tid_, slot_;
    std::int64_t begin_qpc_;
    std::uint64_t toc_, caller_, args_[8];
    std::uint64_t generation_, dispatch_code_, dispatch_toc_;
    std::uint64_t span_id_, parent_id_;
};

void haze_job_trace_dump() noexcept;
#else
class HazeJobTraceScope {
public:
    HazeJobTraceScope(unsigned, unsigned, std::uint64_t, std::uint64_t,
        const std::uint64_t*, unsigned) noexcept {}
    void set_slot(std::uint32_t) noexcept {}
    void set_dispatch(std::uint64_t, std::uint64_t) noexcept {}
};

inline void haze_job_trace_dump() noexcept {}
#endif

#endif
