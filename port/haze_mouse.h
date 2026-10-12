#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

// Relative device counts survive any number of pad polls and are consumed
// once by the simulation. There is no time scaling, deadzone or speed cap.
class HazeMouseMotion {
    std::mutex mutex_;
    int64_t x_ = 0, y_ = 0;
public:
    void add(int32_t x, int32_t y) {
        std::lock_guard lock(mutex_);
        x_ += x; y_ += y;
    }
    void clear() { double x, y; take(x, y); }
    void take(double& x, double& y) {
        std::lock_guard lock(mutex_);
        x = static_cast<double>(x_); y = static_cast<double>(y_);
        x_ = y_ = 0;
    }
};

struct HazeLookAngles { float yaw, pitch; };
inline HazeLookAngles haze_mouse_look(float yaw, float pitch, double x, double y,
                                     double degrees_per_count, bool invert_y,
                                     float lower, float upper) {
    constexpr double pi = 3.14159265358979323846;
    const double scale = degrees_per_count * pi / 180.0;
    return {static_cast<float>(std::remainder(yaw - x * scale, 2.0 * pi)),
            static_cast<float>(std::clamp(pitch + (invert_y ? y : -y) * scale,
                                          double(lower), double(upper)))};
}
