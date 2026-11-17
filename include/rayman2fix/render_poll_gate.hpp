#pragma once
#include <cstdint>

namespace r2fix {

// Pure policy for scheduling game-engine observation from original GLI
// callbacks. There is no thread creation, audio dispatch or key handling.
// The original 3D-camera callback establishes the trusted engine thread.
// When it goes quiet (notably cinematic 4:3 paths), the native viewport
// and native 2D draw hooks may take over, but ONLY on that same thread.
enum class RenderPollSource : std::uint8_t {
    Camera, Native2D, Viewport
};

class RenderPollGate {
public:
    static constexpr std::uint32_t kCameraStaleMs = 48;
    static constexpr std::uint32_t kMinimumIntervalMs = 12;

    bool take(RenderPollSource source, std::uint32_t now,
              std::uint32_t current_thread,
              std::uint32_t camera_thread,
              std::uint32_t camera_last_seen) {
        if (!camera_thread || current_thread != camera_thread)
            return false;
        if (source != RenderPollSource::Camera &&
            (!camera_last_seen ||
             std::uint32_t(now - camera_last_seen) < kCameraStaleMs))
            return false;
        if (has_sample_ &&
            std::uint32_t(now - last_sample_) < kMinimumIntervalMs)
            return false;
        last_sample_ = now;
        has_sample_ = true;
        return true;
    }

private:
    bool has_sample_ = false;
    std::uint32_t last_sample_ = 0;
};

} // namespace r2fix
