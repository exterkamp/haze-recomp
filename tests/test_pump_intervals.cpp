/* Standalone test of the real pump accounting headers.
 *
 * Build with a C++17 compiler, for example:
 *   clang++ -std=c++17 tests/test_pump_intervals.cpp -o test_pump_intervals
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* haze_pump_profile.h only needs this small Win32 surface. Mock it here so
 * accounting and ring behavior can be tested without a window or game run. */
using LONGLONG = long long;
using HANDLE = void*;
struct LARGE_INTEGER { LONGLONG QuadPart; };
struct FILETIME { std::uint32_t dwLowDateTime, dwHighDateTime; };

static LONGLONG g_mock_qpc = 0;
static LONGLONG g_mock_frequency = 1000;
static std::uint64_t g_mock_cpu_100ns = 0;
static bool g_mock_thread_times_valid = true;

static int QueryPerformanceFrequency(LARGE_INTEGER* value)
{
    value->QuadPart = g_mock_frequency;
    return 1;
}

static int QueryPerformanceCounter(LARGE_INTEGER* value)
{
    value->QuadPart = g_mock_qpc;
    return 1;
}

static HANDLE GetCurrentThread() { return reinterpret_cast<HANDLE>(1); }

static FILETIME mock_filetime(std::uint64_t value)
{
    return FILETIME{static_cast<std::uint32_t>(value),
                    static_cast<std::uint32_t>(value >> 32)};
}

static int GetThreadTimes(HANDLE, FILETIME* creation, FILETIME* exit,
                          FILETIME* kernel, FILETIME* user)
{
    *creation = mock_filetime(0);
    *exit = mock_filetime(0);
    *kernel = mock_filetime(g_mock_cpu_100ns);
    *user = mock_filetime(0);
    return g_mock_thread_times_valid ? 1 : 0;
}

#ifndef _WIN32
#define _WIN32 1
#endif
#include "../port/haze_pump_profile.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static bool test_startup_partial_and_consecutive()
{
    auto* p = new HazePumpProfile();
    p->enabled = true;
    p->frequency = 1000;
    p->epoch = 0;
    p->wait_control = nullptr;
    p->csv_path = "pump_intervals_unused.csv";

    // Work before the first present is startup work and must not create a row.
    p->note_wait(true);
    p->add_wait(10);
    p->add_fifo(20);
    p->add_tick(3);
    g_mock_qpc = 1100;
    g_mock_cpu_100ns = 100000;
    p->finish_present(1000, 1, 1);
    CHECK(p->recorded == 0);
    CHECK(p->intervals.previous == 1100);
    CHECK(p->intervals.pending.wait == 0);
    CHECK(p->intervals.pending.fifo == 0);

    // Two drains and one wait accumulate until the next actual present.
    p->note_wait(false);
    p->add_wait(12);
    p->add_fifo(4);
    p->add_fifo(6);
    p->add_tick(3);
    g_mock_qpc = 1300;
    g_mock_cpu_100ns = 120000;
    p->finish_present(1260, 2, 2);
    CHECK(p->recorded == 1);
    const HazePumpInterval& partial = p->rows[0];
    CHECK(partial.begin == 1100 && partial.end == 1300);
    CHECK(partial.frame == 2 && partial.flips == 2);
    CHECK(partial.wait == 12 && partial.fifo == 10 && partial.tick == 3);
    CHECK(partial.present == 40);
    CHECK(partial.waits == 1 && partial.queued_waits == 0 && partial.drains == 2);
    CHECK(partial.cpu_valid && partial.cpu_100ns == 20000);

    // Consecutive pending-flip callbacks have a present scope but no new drain.
    g_mock_qpc = 1400;
    g_mock_cpu_100ns = 130000;
    p->finish_present(1350, 3, 1);
    CHECK(p->recorded == 2);
    const HazePumpInterval& consecutive = p->rows[1];
    CHECK(consecutive.begin == 1300 && consecutive.end == 1400);
    CHECK(consecutive.frame == 3 && consecutive.flips == 1);
    CHECK(consecutive.wait == 0 && consecutive.fifo == 0 && consecutive.tick == 0);
    CHECK(consecutive.present == 50 && consecutive.drains == 0 && consecutive.waits == 0);
    CHECK(consecutive.cpu_valid && consecutive.cpu_100ns == 10000);

    // Model a pre-drain pending present, then a drain that reaches another flip.
    p->add_fifo(8);
    g_mock_qpc = 1500;
    g_mock_cpu_100ns = 140000;
    p->finish_present(1450, 4, 1);
    CHECK(p->recorded == 3 && p->rows[2].fifo == 8 && p->rows[2].drains == 1);
    p->add_fifo(9);
    g_mock_qpc = 1600;
    g_mock_cpu_100ns = 150000;
    p->finish_present(1550, 5, 1);
    CHECK(p->recorded == 4 && p->rows[3].fifo == 9 && p->rows[3].drains == 1);

    delete p;
    return true;
}

static bool test_cpu_validity_and_recovery()
{
    HazePumpIntervals intervals;
    HazePumpInterval row{};
    CHECK(!intervals.finish(100, 10, true, 1, 1, row)); // first sample is startup
    CHECK(intervals.finish(200, 20, false, 2, 1, row)); // invalid current sample
    CHECK(!row.cpu_valid && row.cpu_100ns == 0);
    CHECK(intervals.finish(300, 40, true, 3, 1, row)); // prior sample invalid
    CHECK(!row.cpu_valid);
    CHECK(intervals.finish(400, 90, true, 4, 1, row));
    CHECK(row.cpu_valid && row.cpu_100ns == 50);
    CHECK(intervals.finish(500, 70, true, 5, 1, row)); // counter moved backward
    CHECK(!row.cpu_valid && row.cpu_100ns == 0);
    CHECK(intervals.finish(600, 120, true, 6, 1, row)); // recovers from new baseline
    CHECK(row.cpu_valid && row.cpu_100ns == 50);
    return true;
}

static bool test_report_reset_is_independent()
{
    auto* p = new HazePumpProfile();
    p->enabled = true;
    p->frequency = 1000;
    p->epoch = 0;
    p->wait_control = nullptr;
    p->csv_path = "pump_intervals_unused.csv";
    p->add_wait(7);
    p->add_fifo(11);
    p->add_tick(13);
    p->note_wait(true);
    p->add_fifo(6);

    g_mock_qpc = 2000;
    p->report();
    CHECK(p->wait == 0 && p->fifo == 0 && p->tick == 0);
    CHECK(p->drains == 0 && p->waits == 0 && p->queued_waits == 0);
    CHECK(p->intervals.pending.fifo == 17);
    CHECK(p->intervals.pending.drains == 2);

    delete p;
    return true;
}

static bool test_ring_wrap_and_dump()
{
    const char* path = "pump_intervals_ring_test.csv";
    auto* p = new HazePumpProfile();
    p->enabled = true;
    p->frequency = 1000;
    p->csv_path = path;
    p->wait_control = nullptr;
    p->recorded = 0;
    p->intervals.previous = 1;
    p->intervals.previous_cpu = 1000;
    p->intervals.previous_cpu_valid = true;

    const unsigned total = HazePumpProfile::capacity + 2;
    for (unsigned i = 0; i < total; ++i) {
        g_mock_qpc = 2000 + static_cast<LONGLONG>(i) * 20;
        g_mock_cpu_100ns += 1000;
        p->add_fifo(2);
        p->finish_present(g_mock_qpc - 10, i + 1, i % 4);
    }
    CHECK(p->recorded == total);
    CHECK(p->rows[2].frame == 3);
    CHECK(p->rows[1].frame == total);
    p->dump();

    FILE* csv = std::fopen(path, "rb");
    CHECK(csv != nullptr);
    char line[512];
    CHECK(std::fgets(line, sizeof(line), csv) != nullptr); // header
    CHECK(std::fgets(line, sizeof(line), csv) != nullptr);
    long long frequency = 0, begin = 0, end = 0;
    unsigned frame = 0;
    CHECK(std::sscanf(line, "%lld,%u,%*u,%lld,%lld", &frequency, &frame,
                      &begin, &end) == 4);
    CHECK(frequency == 1000 && frame == 3);
    unsigned rows = 1;
    unsigned last_frame = frame;
    while (std::fgets(line, sizeof(line), csv)) {
        CHECK(std::sscanf(line, "%lld,%u", &frequency, &last_frame) == 2);
        ++rows;
    }
    std::fclose(csv);
    std::remove(path);
    CHECK(rows == HazePumpProfile::capacity);
    CHECK(last_frame == total);

    delete p;
    return true;
}

int main()
{
    if (!test_startup_partial_and_consecutive()) return 1;
    if (!test_cpu_validity_and_recovery()) return 1;
    if (!test_report_reset_is_independent()) return 1;
    if (!test_ring_wrap_and_dump()) return 1;
    std::puts("pump interval accounting: PASS");
    return 0;
}
