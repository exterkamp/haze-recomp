#pragma once
#include <cstdint>
#include <cstring>
#include <mutex>

// Only these disc videos are startup branding or the menu's idle demo.
// Drive the title's own skip path until its player stops; never patch game
// state, invent decoded frames, or inject buttons after a stream has closed.
class HazeStartupSkip {
    std::mutex mutex;
    bool active = false;
    uint32_t owner = 0;
    uint64_t started = 0;
public:
    static bool known_video(const char* uri) {
        const char* prefix = "x-cell-fs:///dev_bdvd/PS3_GAME/USRDIR/assets/g5/pamf/";
        if (!uri || std::strncmp(uri, prefix, std::strlen(prefix))) return false;
        const char* file = uri + std::strlen(prefix);
        return !std::strcmp(file,"ubi.pam") || !std::strcmp(file,"ubi43.pam") ||
               !std::strcmp(file,"frd.pam") || !std::strcmp(file,"frd43.pam") ||
               !std::strcmp(file,"attract.pam");
    }
    bool playback(uint32_t player, const char* uri, bool playing, uint64_t now) {
        std::lock_guard lock(mutex);
        if (!playing) {
            if (owner == player) active = false;
            return false;
        }
        if (!known_video(uri)) return false;
        owner = player; started = now; active = true;
        return true;
    }
    bool sample(uint64_t now, bool& pressed) {
        std::lock_guard lock(mutex);
        pressed = false;
        if (!active) return false;
        uint64_t elapsed = now - started;
        if (elapsed >= 10000) { active = false; return false; }
        // Give the guest time to consume START completion before skipping.
        // Repeat with a release interval so slow frames still see an edge.
        pressed = elapsed >= 300 && (elapsed - 300) % 700 < 400;
        return true;
    }
};
