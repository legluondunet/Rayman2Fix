#pragma once
#include <cstdint>

namespace r2fix {

// Pure policy for scheduling game-engine observation from original GLI
// callbacks. There is no thread creation, audio dispatch or key handling.
// The original 3D-camera callback establishes the trusted engine thread.
// When it goes quiet (notably cinematic 4:3 paths), the native viewport,
// native 2D and original game GLI frame-flip callbacks may take over, but
// ONLY on that same trusted thread. A frame-flip fallback is specifically
// useful when the cinematic renders without camera/2D/viewport callbacks.
enum class RenderPollSource : std::uint8_t {
    Camera, Native2D, Viewport, FrameFlip
};

// An original game frame-flip callback may substitute for a dormant
// camera / native-2D / viewport only in a recognized cinematic or
// during a source-scoped pending ESC-menu window. The camera must have
// established the trusted game thread first. This pure policy does
// NOT authorize sound commands or arbitrary thread migration.
constexpr bool should_poll_game_frame_flip(bool jail_20_intro,
                                           bool esc_menu_pending,
                                           bool camera_thread_known,
                                           bool same_camera_thread) noexcept {
    return (jail_20_intro || esc_menu_pending) &&
           camera_thread_known && same_camera_thread;
}

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
