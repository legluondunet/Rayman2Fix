#pragma once
// Pure SDL3 semantic mapping. No game hooks or SDL calls in this layer.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace r2fix {

enum class PadContext { inactive, gameplay, menu, video, controller_info, cinematic, pause_menu };
enum class PadAction : unsigned {
    left, right, up, down, jump, fire, pause, camera_left, camera_right,
    target, statistics, world_knowledge, murphy, confirm, cancel, skip
};
constexpr std::uint32_t pad_action_bit(PadAction a) noexcept {
    return std::uint32_t{1} << static_cast<unsigned>(a);
}

struct PadSample {
    std::uint32_t device_id = 0;
    bool connected = false;
    bool foreground = false;
    PadContext context = PadContext::inactive;
    int left_x = 0, left_y = 0, right_x = 0;
    int left_trigger = 0, right_trigger = 0;
    bool dpad_left = false, dpad_right = false, dpad_up = false, dpad_down = false;
    bool south = false, east = false, west = false, north = false;
    bool start = false, left_shoulder = false, right_shoulder = false;
};

constexpr int kPadAxisDeadzone = 8192;
constexpr int kPadTriggerThreshold = 8192; // Provisional, verify on hardware.
constexpr bool pad_eligible(const PadSample& s) noexcept {
    return s.connected && s.foreground && s.device_id &&
           s.context != PadContext::inactive;
}

struct PadMovement { float x = 0, y = 0; };

// SDL signed endpoints are asymmetric. Preserve direction and proportional
// magnitude after a radial deadzone, without faster diagonal movement.
inline PadMovement pad_movement(const PadSample& s) noexcept {
    if (!pad_eligible(s) || s.context != PadContext::gameplay) return {};
    const auto normalize = [](int v) {
        v = std::clamp(v, -32768, 32767);
        return v < 0 ? float(v) / 32768.0f : float(v) / 32767.0f;
    };
    const float x = normalize(s.left_x), y = normalize(s.left_y);
    const float magnitude = std::sqrt(x*x + y*y);
    constexpr float deadzone = float(kPadAxisDeadzone) / 32768.0f;
    if (magnitude <= deadzone) return {};
    const float scaled = (std::min(magnitude, 1.0f) - deadzone) / (1.0f - deadzone);
    return {x / magnitude * scaled, y / magnitude * scaled};
}

constexpr std::uint32_t pad_actions(const PadSample& s) noexcept {
    if (!pad_eligible(s)) return 0;
    const auto bit = [](bool on, PadAction a) {
        return on ? pad_action_bit(a) : 0u;
    };
    if (s.context == PadContext::cinematic)
        return bit(s.south, PadAction::skip);
    if (s.context == PadContext::menu || s.context == PadContext::pause_menu || s.context == PadContext::video ||
        s.context == PadContext::controller_info) {
        // Information pages are read-only: only back is accepted.
        if (s.context == PadContext::video || s.context == PadContext::controller_info)
            return bit(s.east, PadAction::cancel);
        const bool left = s.dpad_left || s.left_x < -kPadAxisDeadzone;
        const bool right = s.dpad_right || s.left_x > kPadAxisDeadzone;
        const bool up = s.dpad_up || s.left_y < -kPadAxisDeadzone;
        const bool down = s.dpad_down || s.left_y > kPadAxisDeadzone;
        return bit(left && !right, PadAction::left) |
               bit(right && !left, PadAction::right) |
               bit(up && !down, PadAction::up) |
               bit(down && !up, PadAction::down) |
               bit(s.south, PadAction::confirm) |
               bit(s.east, PadAction::cancel) |
               bit(s.start, PadAction::pause);
    }
    // Dreamcast positions + modern horizontal right stick and shoulders.
    const bool cam_left = s.west || s.left_shoulder || s.right_x < -kPadAxisDeadzone;
    const bool cam_right = s.north || s.right_shoulder || s.right_x > kPadAxisDeadzone;
    return bit(s.south, PadAction::jump) |
           bit(s.east, PadAction::fire) |
           bit(s.start, PadAction::pause) |
           bit(cam_left && !cam_right, PadAction::camera_left) |
           bit(cam_right && !cam_left, PadAction::camera_right) |
           bit(s.left_trigger > kPadTriggerThreshold, PadAction::target) |
           bit(s.right_trigger > kPadTriggerThreshold, PadAction::statistics) |
           bit(s.dpad_up, PadAction::world_knowledge) |
           bit(s.dpad_down, PadAction::murphy);
}

struct PadTransitions {
    std::uint32_t held = 0, pressed = 0, released = 0;
    bool eligible = false;
    PadMovement movement{};
};

// Physical neutral is independent of the semantic context. In particular,
// a button ignored by an information page cannot leak into gameplay.
inline bool pad_physically_active(const PadSample& s) noexcept {
    const auto x = std::int64_t(std::clamp(s.left_x, -32768, 32767));
    const auto y = std::int64_t(std::clamp(s.left_y, -32768, 32767));
    return s.south || s.east || s.west || s.north || s.start ||
        s.left_shoulder || s.right_shoulder ||
        s.dpad_left || s.dpad_right || s.dpad_up || s.dpad_down ||
        s.left_trigger > kPadTriggerThreshold ||
        s.right_trigger > kPadTriggerThreshold ||
        s.right_x < -kPadAxisDeadzone || s.right_x > kPadAxisDeadzone ||
        x*x + y*y > std::int64_t(kPadAxisDeadzone)*kPadAxisDeadzone;
}

// One owner, one semantic context. Require neutral physical controls after
// attach, focus restoration, owner change and context change; neither a held
// confirmation nor an ignored button nor analog motion may cross the boundary.
class PadTransitionGate {
public:
    PadTransitions update(const PadSample& sample) noexcept {
        if (!pad_eligible(sample)) {
            const auto released = held_;
            held_ = owner_ = 0;
            context_ = PadContext::inactive;
            blocked_ = true;
            return {0, 0, released, false, {}};
        }
        if (owner_ != sample.device_id || context_ != sample.context) {
            const auto released = held_;
            held_ = 0;
            owner_ = sample.device_id;
            context_ = sample.context;
            blocked_ = pad_physically_active(sample);
            return {0, 0, released, true, {}};
        }
        if (blocked_) {
            blocked_ = pad_physically_active(sample);
            return {0, 0, 0, true, {}};
        }
        const auto next = pad_actions(sample);
        const PadTransitions result{next, next & ~held_, held_ & ~next,
                                    true, pad_movement(sample)};
        held_ = next;
        return result;
    }
private:
    std::uint32_t owner_ = 0, held_ = 0;
    PadContext context_ = PadContext::inactive;
    bool blocked_ = true;
};

} // namespace r2fix
