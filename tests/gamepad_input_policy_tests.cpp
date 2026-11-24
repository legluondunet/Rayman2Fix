#include "rayman2fix/gamepad_input_policy.hpp"
#include "rayman2fix/gamepad_control_info.hpp"
#include "rayman2fix/gamepad_menu_bridge.hpp"
#include "rayman2fix/native_ipt_probe.hpp"
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
    check(!gate.update(a).held, "initial held button suppressed");
    a.south = false; check(!gate.update(a).held, "initial neutral rearms");
    a.south = true;
    e = gate.update(a);
    check(e.eligible && has(e.pressed, PadAction::jump), "south jumps");
    e = gate.update(a);
    check(!e.pressed && !e.released && has(e.held, PadAction::jump), "no repeat");
    a.foreground = false;
    e = gate.update(a);
    check(!e.eligible && has(e.released, PadAction::jump) && !e.held, "focus release");
    check(!gate.update(a).released, "no duplicate release");
    a.foreground = true;
    check(!gate.update(a).held, "focus restored with held button suppressed");
    a.south = false; gate.update(a);
    a.south = true; check(has(gate.update(a).pressed, PadAction::jump), "fresh focus press");
    a.connected = false;
    check(has(gate.update(a).released, PadAction::jump), "unplug release");
    check(!gate.update(a).released, "unplug once");
    a.connected = true; a.device_id = 8;
    check(!gate.update(a).held, "reconnected held button suppressed");
    a.south = false; gate.update(a);
    a.south = true; check(has(gate.update(a).pressed, PadAction::jump), "fresh reconnect press");
    a.device_id = 9;
    e = gate.update(a);
    check(e.eligible && !e.held && has(e.released, PadAction::jump), "switch releases");
    check(!gate.update(a).held, "new owner held suppressed");
    a.south = false; gate.update(a);
    a.south = true; check(has(gate.update(a).pressed, PadAction::jump), "fresh owner press");
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
    a = active(PadContext::menu); contexts.update(a); a.south = true;
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

    // Held input ignored by an info screen must not become a jump later.
    PadTransitionGate info;
    a = active(PadContext::controller_info); info.update(a);
    a.south = true; check(!info.update(a).held, "info ignores held south");
    a.context = PadContext::gameplay;
    check(!info.update(a).held && !info.update(a).held, "ignored button cannot leak");
    a.south = false; a.left_x = a.left_y = 7000;
    check(!info.update(a).movement.x, "diagonal still outside radial deadzone");
    a.left_x = a.left_y = 0; info.update(a);
    a.left_x = 16000;
    check(info.update(a).movement.x > 0, "fresh movement accepted");
    a.foreground = false;
    check(!info.update(a).movement.x, "unfocused analog neutral");
    a.foreground = true;
    check(!info.update(a).movement.x && !info.update(a).movement.x,
          "held analog cannot cross focus boundary");

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
    // Native DirectInput keyboard merge never replaces physical keyboard
    // state or changes failed reads, unauthenticated contexts or background.
    std::uint8_t keys[256]{};
    keys[0x39] = 0x80; // unrelated physical Space
    keys[0xC8] = 0x80; // physical Up
    const auto menu_actions = pad_action_bit(PadAction::up) |
        pad_action_bit(PadAction::down) | pad_action_bit(PadAction::confirm) |
        pad_action_bit(PadAction::cancel) | pad_action_bit(PadAction::jump);
    check(!merge_pad_menu_keys(PadContext::inactive, true, 0, 256, keys, menu_actions),
          "unknown scene cannot inject");
    check(!merge_pad_menu_keys(PadContext::gameplay, true, 0, 256, keys, menu_actions),
          "gameplay not routed through menu bridge");
    check(!merge_pad_menu_keys(PadContext::menu, false, 0, 256, keys, menu_actions),
          "background cannot inject");
    check(!merge_pad_menu_keys(PadContext::menu, true, -1, 256, keys, menu_actions) &&
          !merge_pad_menu_keys(PadContext::menu, true, 0, 255, keys, menu_actions),
          "invalid native keyboard read cannot inject");
    check(!keys[0x1C] && !keys[0x01] && !keys[0xD0] && keys[0x39] == 0x80,
          "refused buffers untouched");
    check(merge_pad_menu_keys(PadContext::menu, true, 0, 256, keys, menu_actions) == 3 &&
          keys[0xC8] == 0x80 && keys[0xD0] == 0x80 &&
          keys[0x1C] == 0x80 && keys[0x01] == 0x80 && keys[0x39] == 0x80,
          "merge preserves existing physical state and adds menu-only keys");
    std::uint8_t video_keys[256]{};
    check(merge_pad_menu_keys(PadContext::video, true, 0, 256, video_keys, menu_actions) == 1 &&
          video_keys[0x01] == 0x80 && !video_keys[0x1C] &&
          !video_keys[0xC8] && !video_keys[0xD0], "VIDEO receives only back");

    // Candidate retail IPT inspection rejects implausible addresses/layouts.
    check(!ipt_header_plausible({0,0}) &&
          !ipt_header_plausible({513,0x10000}) &&
          !ipt_header_plausible({28,0xfffffffcu}) &&
          !ipt_header_plausible({28,0x10001}) &&
          ipt_header_plausible({128,0x10000}), "bounded candidate table");
    check(!ipt_entry_plausible({1,0,0x10000,0,0,1}) &&
          !ipt_entry_plausible({1,0x10000,0x10000,0,0,2}) &&
          ipt_entry_plausible({1,0x10000,0x10000,1,0,1}), "candidate entry bounds");
    IptNames native_names{};
    for(auto& n:native_names) std::strcpy(n.data(),"candidate");
    std::strcpy(native_names[0].data(),"Pad_AxeX");
    std::strcpy(native_names[1].data(),"Pad_AxeY");
    std::strcpy(native_names[10].data(),"Action_Sauter");
    std::strcpy(native_names[11].data(),"Action_Tirer");
    check(ipt_reference_anchors(native_names)==15u,"four distinct name anchors");
    native_names[11].fill('x');
    check(!ipt_name_plausible(native_names[11]) && !ipt_reference_anchors(native_names),
          "unterminated name refuses candidate");
    check(!ipt_entry_changed({0,0,0,1,0,1},{0,0,0,12,0,1}) &&
           ipt_entry_changed({0,0,0,1,0,1},{0,0,0,-1,0,1}) &&
           ipt_entry_changed({0,0,0,1,0,1},{0,0,0,1,0x3f000000,1}),
           "probe ignores age counters, records sign or analog changes");

    IptActionReadback byte_active{1,0x10000,0x10000,1,0,1,{0xff,0xaa,0x55}};
    check(ipt_entry_plausible(byte_active),"native active is a byte; padding is not BOOL");
    check(ipt_entry_plausible({0,0,0,0,0,0}),"unused native entry allowed");
    check(ipt_calibrated_axis(-1.f,-50,2,50)==-50.f &&
          ipt_calibrated_axis(1.f,-50,2,50)==50.f &&
          ipt_calibrated_axis(0.f,-50,2,50)==2.f &&
          ipt_calibrated_axis(.5f,-50,2,50)==26.f,"signed native calibration");
    a=active(PadContext::pause_menu);a.south=true;a.dpad_up=true;
    check(has(pad_actions(a),PadAction::confirm) && has(pad_actions(a),PadAction::up),
          "verified in-game pause has menu semantics");
    std::cout << "SDL3 Dreamcast mapping / focus / hotplug policy: OK\n";
}
