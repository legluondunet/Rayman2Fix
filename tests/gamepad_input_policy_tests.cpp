#include "rayman2fix/gamepad_input_policy.hpp"
#include "rayman2fix/gamepad_control_info.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace r2fix;
void check(bool v, const char* msg) { if (!v) throw std::runtime_error(msg); }
bool has(std::uint32_t actions, PadAction a) { return (actions & pad_action_bit(a)) != 0; }
PadSample active(PadContext context = PadContext::gameplay) {
    PadSample s{};
    s.device_id = 7; s.connected = true; s.foreground = true; s.context = context;
    return s;
}
int main() {
    PadTransitionGate gate;
    auto a = active();
    a.south = true;
    auto e = gate.update({});
    check(!e.eligible && !e.held && !e.pressed, "no pad neutral");
    e = gate.update(a);
    check(e.eligible && has(e.pressed, PadAction::jump), "south jumps");
    e = gate.update(a);
    check(!e.pressed && !e.released && has(e.held, PadAction::jump), "no repeat");
    a.foreground = false;
    e = gate.update(a);
    check(!e.eligible && has(e.released, PadAction::jump) && !e.held, "focus release");
    check(!gate.update(a).released, "no duplicate release");
    a.foreground = true;
    check(has(gate.update(a).pressed, PadAction::jump), "focus restored");
    a.connected = false;
    check(has(gate.update(a).released, PadAction::jump), "unplug release");
    check(!gate.update(a).released, "unplug once");
    a.connected = true; a.device_id = 8;
    check(has(gate.update(a).pressed, PadAction::jump), "reconnect");
    a.device_id = 9;
    e = gate.update(a);
    check(!e.eligible && has(e.released, PadAction::jump), "switch releases");
    check(has(gate.update(a).pressed, PadAction::jump), "fresh owner");
    a.device_id = 0;
    check(!gate.update(a).eligible, "invalid device");

    a = active();
    a.east = a.start = a.dpad_up = a.dpad_down = true;
    a.left_trigger = a.right_trigger = 32767;
    check(pad_actions(a) == (pad_action_bit(PadAction::fire) |
        pad_action_bit(PadAction::pause) | pad_action_bit(PadAction::target) |
        pad_action_bit(PadAction::statistics) | pad_action_bit(PadAction::world_knowledge) |
        pad_action_bit(PadAction::murphy)), "Dreamcast independent actions");
    a = active();
    a.west = true;
    check(pad_actions(a) == pad_action_bit(PadAction::camera_left), "west camera left");
    a.west = false; a.north = true;
    check(pad_actions(a) == pad_action_bit(PadAction::camera_right), "north camera right");
    a.north = false; a.left_shoulder = true;
    check(pad_actions(a) == pad_action_bit(PadAction::camera_left), "LB camera left");
    a.left_shoulder = false; a.right_shoulder = true;
    check(pad_actions(a) == pad_action_bit(PadAction::camera_right), "RB camera right");
    a.right_shoulder = false; a.right_x = -32768;
    check(pad_actions(a) == pad_action_bit(PadAction::camera_left), "right stick left");
    a.right_x = 32767;
    check(pad_actions(a) == pad_action_bit(PadAction::camera_right), "right stick right");
    a.west = true;
    check(!pad_actions(a), "opposing camera inputs neutral");
    a = active();
    a.left_trigger = a.right_trigger = kPadTriggerThreshold;
    check(!pad_actions(a), "trigger drift ignored");

    a = active();
    a.left_x = -8192;
    check(pad_movement(a).x == 0, "deadzone");
    a.left_x = 16384;
    const auto half = pad_movement(a);
    check(half.x > .30f && half.x < .35f, "proportional walking");
    a.left_x = -32768;
    check(pad_movement(a).x == -1.0f, "full negative");
    a.left_x = 32767; a.left_y = 32767;
    const auto diagonal = pad_movement(a);
    check(std::abs(std::hypot(diagonal.x, diagonal.y) - 1.0f) < .0001f,
        "diagonal normalized");
    a.foreground = false;
    check(!pad_movement(a).x && !pad_movement(a).y, "unfocused neutral axes");

    a = active(PadContext::menu);
    a.dpad_up = a.south = true;
    check(pad_actions(a) == (pad_action_bit(PadAction::up) | pad_action_bit(PadAction::confirm)),
        "menu navigation and confirm distinct from gameplay");
    a.left_y = 32767;
    check(!has(pad_actions(a), PadAction::up) && !has(pad_actions(a), PadAction::down),
        "contradictory menu directions neutral");
    check(!pad_movement(a).y, "menu no analog motion");
    a.context = PadContext::video; a.east = true;
    check(pad_actions(a) == pad_action_bit(PadAction::cancel), "video only back");
    a.context = PadContext::cinematic;
    check(pad_actions(a) == pad_action_bit(PadAction::skip), "cinematic south skip");
    a.context = PadContext::inactive;
    check(!pad_actions(a), "unknown context fail closed");

    PadTransitionGate contexts;
    a = active(PadContext::menu); a.south = true;
    check(has(contexts.update(a).pressed, PadAction::confirm), "menu confirm");
    a.context = PadContext::cinematic;
    e = contexts.update(a);
    check(has(e.released, PadAction::confirm) && !e.held, "context releases");
    a.context = PadContext::gameplay;
    check(!contexts.update(a).held, "second context cannot bypass release");
    check(!contexts.update(a).held, "held confirm cannot jump");
    a.south = false; check(!contexts.update(a).held, "neutral rearms");
    a.south = true;
    check(has(contexts.update(a).pressed, PadAction::jump), "fresh gameplay press");
    a = active(PadContext::controller_info);
    a.south = a.west = a.north = a.start = a.dpad_up = a.dpad_down = true;
    a.left_x = a.right_x = 32767;
    a.left_trigger = a.right_trigger = 32767;
    check(!pad_actions(a) && !pad_movement(a).x,
        "controller info ignores all non-return input");
    a.east = true;
    check(pad_actions(a) == pad_action_bit(PadAction::cancel),
        "controller info only back");
    a.foreground = false;
    check(!pad_actions(a), "controller info background ignores back");

    check(kPadControlRows.size() == 11, "all approved controls presented");
    for (const auto& row : kPadControlRows) {
        check(!row.action_key.empty() && !row.generic.empty() &&
            !row.xbox.empty() && !row.playstation.empty(), "complete label rows");
    }
    check(pad_display_family(false, PadLabelFamily::xbox) == PadLabelFamily::generic,
        "disconnected never claims Xbox");
    check(pad_display_family(true, PadLabelFamily::playstation) == PadLabelFamily::playstation,
        "verified connected family");
    check(pad_control_label(kPadControlRows[1], PadLabelFamily::xbox) == "A" &&
        pad_control_label(kPadControlRows[1], PadLabelFamily::playstation) == "Cross" &&
        pad_control_label(kPadControlRows[1], PadLabelFamily::generic) == "South",
        "physical south labeling");
    check(pad_control_label(kPadControlRows[2], PadLabelFamily::playstation) == "Circle" &&
        pad_back_label(PadLabelFamily::playstation) == "Circle" &&
        pad_back_label(PadLabelFamily::xbox) == "B" &&
        pad_back_label(PadLabelFamily::generic) == "East",
        "physical east labeling");
    std::cout << "SDL3 Dreamcast mapping / focus / hotplug policy: OK\n";
}
