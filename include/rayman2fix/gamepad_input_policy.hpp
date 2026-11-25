#pragma once
// Pure SDL3 semantic mapping. No game hooks or SDL calls in this layer.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace r2fix {

enum class PadContext { inactive, gameplay, menu, video, controller_info, cinematic, pause_menu, first_cinematic, learn_cinematic, world_map };
enum class PadAction : unsigned {
    left, right, up, down, jump, fire, pause, camera_left, camera_right,
    target, statistics, world_knowledge, murphy, confirm, cancel, skip
};
constexpr std::uint32_t pad_action_bit(PadAction a) noexcept {
    return std::uint32_t{1} << static_cast<unsigned>(a);
}

// Retail IPT reference indices; native live-entry validation remains mandatory.
// The PC action for Murfy/knowledge is shared. Swimming uses separate
// native entries, with the original actor script deciding when to consume them.
struct PadIptBinding {
    unsigned index;
    std::string_view name_fragment;
    std::uint32_t actions;
};
inline constexpr PadIptBinding kPadGameplayIptBindings[] = {
    {10, "sauter", pad_action_bit(PadAction::jump)},
    {11, "tirer", pad_action_bit(PadAction::fire)},
    {13, "strafe", pad_action_bit(PadAction::target)},
    {14, "plonger", pad_action_bit(PadAction::target)},
    {15, "remonter", pad_action_bit(PadAction::jump)},
    {16, "tournedroite", pad_action_bit(PadAction::camera_right)},
    {17, "tournegauche", pad_action_bit(PadAction::camera_left)},
    {21, "textesuivant", pad_action_bit(PadAction::jump)},
    {22, "appelermurphy", pad_action_bit(PadAction::murphy) |
                         pad_action_bit(PadAction::world_knowledge)},
    {24, "affiche_jauge", pad_action_bit(PadAction::statistics)}
};
constexpr const PadIptBinding* pad_gameplay_ipt_binding(unsigned index,
                                                       std::uint32_t actions) noexcept {
    for (const auto& binding : kPadGameplayIptBindings)
        if (binding.index == index && (binding.actions & actions)) return &binding;
    return nullptr;
}

// Exact PC scene catalog, cross-checked against the PC depot and map
// classification documented in docs/sdl3-gamepad-policy.md. Names alone
// do not authorize input: the live Playing/pause/IPT guards still apply.
inline constexpr std::string_view kPadPlayableScenes[] = {
    "learn_10", "learn_30", "learn_31", "learn_40", "learn_60",
    "bast_10", "bast_20", "bast_22", "ski_10", "ski_60",
    "chase_10", "chase_22", "ly_10", "ly_20", "water_10",
    "water_20", "rodeo_10", "rodeo_40", "rodeo_60", "vulca_10",
    "vulca_20", "glob_10", "glob_20", "glob_30", "whale_00",
    "whale_05", "whale_10", "plum_00", "plum_10", "plum_20",
    "cask_10", "cask_30", "nave_10", "nave_15", "nave_20",
    "seat_10", "seat_11", "earth_10", "earth_20", "earth_30",
    "helic_10", "helic_20", "helic_30", "morb_00", "morb_10",
    "morb_20", "ball", "ile_10", "mine_10", "boat01",
    "boat02", "astro_00", "astro_10", "rhop_10", "bonux",
};
constexpr char pad_ascii_lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}
constexpr bool pad_scene_equal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (pad_ascii_lower(a[i]) != b[i]) return false;
    return true;
}
constexpr std::uint32_t pad_playable_scene_id(std::string_view level) noexcept {
    std::uint32_t id = 1;
    for (auto candidate : kPadPlayableScenes) {
        if (pad_scene_equal(level, candidate)) return id;
        ++id;
    }
    return 0;
}
constexpr bool pad_gameplay_scene_ready(std::uint32_t scene, bool playing,
                                        bool pause_clear, bool learn_handover) noexcept {
    return scene > 0 && scene <= sizeof(kPadPlayableScenes)/sizeof(kPadPlayableScenes[0]) &&
        playing && pause_clear && (scene != 1 || learn_handover);
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
    std::uint32_t scene_id = 0; // exact live scene identity; 0 for non-gameplay catalog scenes
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
    if (s.context == PadContext::cinematic || s.context == PadContext::first_cinematic ||
        s.context == PadContext::learn_cinematic)
        return bit(s.south, PadAction::skip);
    if (s.context == PadContext::menu || s.context == PadContext::pause_menu ||
        s.context == PadContext::world_map || s.context == PadContext::video ||
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
               // Mapmonde uses arrows for the Lums route, but its native
               // jump is a separate IPT action, not Return.
               bit(s.context == PadContext::world_map && s.south, PadAction::jump) |
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

// Controller ownership changes only on an observed neutral-to-active
// transition from another device while the current owner is idle.
// No simultaneous input merging or hotplug steal from a held owner.
constexpr bool pad_may_take_owner(bool foreground,bool owner_active,
                                  bool candidate_armed,bool candidate_active) noexcept {
    return foreground&&!owner_active&&candidate_armed&&candidate_active;
}
// The approved retail DirectInput joystick poll emits a normalized
// button mask at +4 and six signed, center-relative axes at +0xc..0x20.
// Neutralize only those fields, before the native direction/history
// update. Preserve flags, padding, and the keyboard's independent path.
inline bool neutralize_native_joystick(bool sdl_owner,bool fresh,bool supported_context,
    bool poll_success,std::uint32_t* words,std::size_t count) noexcept {
    if(!sdl_owner||!fresh||!supported_context||!poll_success||!words||count!=9u)return false;
    words[1]=0;
    for(unsigned i=3;i<9;++i)words[i]=0;
    return true;
}
// One owner, one semantic context. Require neutral physical controls after
// attach, focus restoration, owner change and context change; neither a held
// confirmation nor an ignored button nor analog motion may cross the boundary.
class PadTransitionGate {
public:
    PadTransitions update(const PadSample& sample) noexcept {
        if (!pad_eligible(sample)) {
            const auto released = held_;
            held_ = owner_ = scene_ = 0;
            context_ = PadContext::inactive;
            blocked_ = true;
            return {0, 0, released, false, {}};
        }
        if (owner_ != sample.device_id || context_ != sample.context || scene_ != sample.scene_id) {
            const auto released = held_;
            held_ = 0;
            owner_ = sample.device_id;
            context_ = sample.context;
            scene_ = sample.scene_id;
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
    std::uint32_t owner_ = 0, held_ = 0, scene_ = 0;
    PadContext context_ = PadContext::inactive;
    bool blocked_ = true;
};

} // namespace r2fix
