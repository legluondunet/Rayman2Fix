#pragma once

// Pure controller-state policy for the future SDL3 gamepad bridge.
// No SDL3 dependency, Win32 keyboard injection, game hooks or threads.
// The adapter must feed only a normalized snapshot and use the transitions
// to update a verified native game-input route (not implemented here).
#include <cstdint>

namespace r2fix {

enum class PadAction : unsigned {
    left, right, up, down, primary, secondary, pause, camera_left, camera_right
};

constexpr std::uint32_t pad_action_bit(PadAction a) noexcept {
    return std::uint32_t{1} << static_cast<unsigned>(a);
}

struct PadSample {
    std::uint32_t device_id = 0; // Stable per SDL3 connection; zero means none.
    bool connected = false;
    bool foreground = false; // True only while the real game window owns focus.
    int left_x = 0; // Raw SDL signed axis: -32768..32767.
    int left_y = 0;
    bool dpad_left = false;
    bool dpad_right = false;
    bool dpad_up = false;
    bool dpad_down = false;
    bool south = false; // Logical primary; native mapping is still TBD.
    bool east = false;
    bool start = false;
    bool left_shoulder = false;
    bool right_shoulder = false;
};

constexpr int kPadAxisDeadzone = 8192;

constexpr std::uint32_t pad_actions(const PadSample& s) noexcept {
    if (!s.connected || !s.foreground || !s.device_id) return 0;
    bool left = s.dpad_left || s.left_x < -kPadAxisDeadzone;
    bool right = s.dpad_right || s.left_x > kPadAxisDeadzone;
    bool up = s.dpad_up || s.left_y < -kPadAxisDeadzone;
    bool down = s.dpad_down || s.left_y > kPadAxisDeadzone;
    // Contradictory inputs are neutral, not two stuck directions.
    if (left && right) left = right = false;
    if (up && down) up = down = false;
    return (left ? pad_action_bit(PadAction::left) : 0) |
           (right ? pad_action_bit(PadAction::right) : 0) |
           (up ? pad_action_bit(PadAction::up) : 0) |
           (down ? pad_action_bit(PadAction::down) : 0) |
           (s.south ? pad_action_bit(PadAction::primary) : 0) |
           (s.east ? pad_action_bit(PadAction::secondary) : 0) |
           (s.start ? pad_action_bit(PadAction::pause) : 0) |
           (s.left_shoulder ? pad_action_bit(PadAction::camera_left) : 0) |
           (s.right_shoulder ? pad_action_bit(PadAction::camera_right) : 0);
}

struct PadTransitions {
    std::uint32_t held = 0;
    std::uint32_t pressed = 0;
    std::uint32_t released = 0;
    bool eligible = false;
};

// One single owner. A focus loss or unplug emits releases ONCE.
// A controller identity switch first releases the old state and requires
// one fresh sample before accepting input from the new pad. This prevents
// transferring held buttons to another device without a release edge.
class PadTransitionGate {
public:
    PadTransitions update(const PadSample& sample) noexcept {
        const bool eligible = sample.connected && sample.foreground &&
                              sample.device_id != 0;
        if (eligible && owner_ != 0 && owner_ != sample.device_id) {
            const std::uint32_t released = held_;
            owner_ = 0;
            held_ = 0;
            return {0, 0, released, false};
        }
        const std::uint32_t next = eligible ? pad_actions(sample) : 0;
        const PadTransitions result = {
            next, next & ~held_, held_ & ~next, eligible
        };
        held_ = next;
        owner_ = eligible ? sample.device_id : 0;
        return result;
    }

private:
    std::uint32_t owner_ = 0;
    std::uint32_t held_ = 0;
};

} // namespace r2fix
