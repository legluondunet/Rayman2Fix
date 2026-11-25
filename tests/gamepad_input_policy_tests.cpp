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
    for(unsigned l=0;l<5;++l) {
        const auto locale=static_cast<PadInfoLocale>(l);
        for(auto family:{PadLabelFamily::generic,PadLabelFamily::xbox,PadLabelFamily::playstation}) {
            const auto view=make_pad_info_view(locale,true,family);
            const auto unplugged=make_pad_info_view(locale,false,family);
            check(!view.status.empty() && !view.back.empty() && !view.back_control.empty(),
                  "localized connected presentation");
            check(unplugged.family==PadLabelFamily::generic &&
                  unplugged.status==pad_info_strings(locale).no_pad &&
                  unplugged.back_control==pad_info_strings(locale).generic_back,
                  "disconnected view immediately returns to generic controls");
            for(unsigned i=0;i<view.rows.size();++i) {
                check(!view.rows[i].action.empty() && !view.rows[i].control.empty(),
                      "localized row completeness");
                check(view.rows[i].pending==(i==8),"unverified knowledge route marked pending");
            }
            check(view.swim_pending && !view.pending_label.empty(),"swimming pending notice");
        }
    }
    const auto fr=make_pad_info_view(PadInfoLocale::fr,true,PadLabelFamily::playstation);
    check(fr.status=="PlayStation" && fr.rows[0].control=="Stick gauche" &&
          fr.rows[1].control=="Cross" && fr.back_control=="Circle",
          "French PlayStation labels follow detected family");
    check(make_pad_info_view(static_cast<PadInfoLocale>(999),false,
          PadLabelFamily::xbox).status=="No controller connected","unknown locale fallback");
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
    std::uint8_t info_keys[256]{};
    info_keys[0x39]=0x80;
    check(merge_pad_menu_keys(PadContext::controller_info,true,0,256,info_keys,
          menu_actions)==1 && info_keys[0x01]==0x80 && info_keys[0x39]==0x80 &&
          !info_keys[0x1C] && !info_keys[0xC8] && !info_keys[0xD0],
          "authenticated controller page receives only back, preserves keyboard");

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
    IptReadbacks retail{};
    for(unsigned i=0;i<retail.size();++i) {
        retail[i].keyword_count=kRetailIptCounts[i];
        retail[i].keywords=0x10000u+i*0x100u;
        retail[i].active=1;
    }
    check(ipt_retail_structure({138,0x20000},retail),"CD stripped-name fingerprint");
    check(!ipt_retail_structure({137,0x20000},retail),"wrong table length");
    retail[10].keyword_count++;
    check(!ipt_retail_structure({138,0x20000},retail),"wrong action layout");
    retail[10].keyword_count--;
    retail[0].keywords=0;
    check(!ipt_retail_structure({138,0x20000},retail),"missing keyword buffer");
    PadTransitionGate movie_gate;
    a=active(PadContext::first_cinematic); movie_gate.update(a);
    a.south=true;
    check(has(movie_gate.update(a).held,PadAction::skip),"first native movie skip");
    a.context=PadContext::cinematic;
    check(!movie_gate.update(a).held,"held skip cannot cross movie boundary");
    a.context=PadContext::learn_cinematic;
    check(!movie_gate.update(a).held,"held skip cannot cross Learn10 boundary");
    a.south=false;movie_gate.update(a);a.south=true;
    check(has(movie_gate.update(a).held,PadAction::skip),"fresh Learn10 skip");
    PadTransitionGate map_gate;
    a=active(PadContext::world_map);map_gate.update(a);
    a.left_x=32767;a.south=true;
    const auto map=map_gate.update(a);
    check(has(map.held,PadAction::right) && has(map.held,PadAction::confirm) &&
          has(map.held,PadAction::jump) && map.movement.x==0.f &&
          !has(map.held,PadAction::fire),
          "world map uses directions and confirm plus native jump, not general gameplay");
    a.south=false;
    check(!has(map_gate.update(a).held,PadAction::jump),"map jump release");
    a.south=true;
    check(has(map_gate.update(a).pressed,PadAction::jump),"fresh map jump");
    a=active(PadContext::menu);a.south=true;
    check(!has(pad_actions(a),PadAction::jump),"ordinary menu has no native jump");
    a=active(PadContext::world_map);a.south=true;a.foreground=false;
    check(!pad_actions(a),"background map jump refused");
    a.foreground=true;
    a.context=PadContext::gameplay;
    check(!map_gate.update(a).held,"map confirm cannot leak into gameplay");
    check(pad_playable_scene_id("Learn_10")==1, "Learn_10 has special handover");
    const auto first=pad_playable_scene_id("LEARN_30");
    const auto next=pad_playable_scene_id("learn_31");
    check(first && next && first!=next, "distinct consecutive gameplay scenes");
    for(auto name:kPadPlayableScenes) {
        const auto id=pad_playable_scene_id(name);
        check(id && pad_gameplay_scene_ready(id,true,true,true), "catalog scene available");
        check(!pad_gameplay_scene_ready(id,false,true,true), "loading refuses gameplay");
        check(!pad_gameplay_scene_ready(id,true,false,true), "pause refuses gameplay");
    }
    for(auto name:{"", "menu", "mapmonde", "jail_10", "jail_20", "bast_09",
                   "batam_10", "batam_20", "nego_10", "poloc_10", "poloc_20",
                   "poloc_30", "poloc_40", "raycap", "end_10", "staff_10",
                   "learn_20", "learn_30_extra", "unrecognized"}) {
        check(!pad_playable_scene_id(name), "non-gameplay map excluded");
    }
    check(!pad_gameplay_scene_ready(1,true,true,false), "Learn10 intro protected");
    check(pad_gameplay_scene_ready(first,true,true,false), "other levels independent of story");
    check(!pad_gameplay_scene_ready(0,true,true,true), "unknown refuses gameplay");
    check(!pad_gameplay_scene_ready(999,true,true,true), "out of range refuses gameplay");
    PadTransitionGate scene_gate;
    a=active();a.scene_id=first;scene_gate.update(a);
    a.south=true;a.left_x=32767;
    check(has(scene_gate.update(a).held,PadAction::jump), "first scene input");
    a.scene_id=next;
    const auto boundary=scene_gate.update(a);
    check(!boundary.held && has(boundary.released,PadAction::jump) &&
          boundary.movement.x==0.f, "same-context level transition releases all");
    check(!scene_gate.update(a).held, "held input across new level blocked");
    a.south=false;a.left_x=0;scene_gate.update(a);
    a.south=true;a.left_x=32767;
    const auto fresh=scene_gate.update(a);
    check(has(fresh.pressed,PadAction::jump) && fresh.movement.x>0.f, "fresh input in next level");
    const auto jump=pad_action_bit(PadAction::jump);
    const auto target=pad_action_bit(PadAction::target);
    const auto knowledge=pad_action_bit(PadAction::world_knowledge);
    const auto murphy=pad_action_bit(PadAction::murphy);
    check(pad_gameplay_ipt_binding(10,jump) && pad_gameplay_ipt_binding(15,jump) &&
          pad_gameplay_ipt_binding(21,jump), "South jumps, resurfaces and advances dialogue");
    check(pad_gameplay_ipt_binding(13,target) && pad_gameplay_ipt_binding(14,target),
          "left trigger targets or dives");
    check(pad_gameplay_ipt_binding(22,knowledge) && pad_gameplay_ipt_binding(22,murphy),
          "both dpad directions use shared PC F1 action");
    check(!pad_gameplay_ipt_binding(14,jump) && !pad_gameplay_ipt_binding(15,target),
          "swimming routes not reversed");
    check(!pad_gameplay_ipt_binding(22,jump) && !pad_gameplay_ipt_binding(22,0),
          "knowledge cannot leak from unrelated buttons");
    for(unsigned index=0;index<138;++index) {
        const auto* b=pad_gameplay_ipt_binding(index,0xffffffffu);
        const bool known=index==10 || index==11 || index==13 || index==14 ||
            index==15 || index==16 || index==17 || index==21 || index==22 || index==24;
        check(bool(b)==known,"only explicit gameplay action indices");
        if(b) check(!b->name_fragment.empty(),"every binding names its reference action");
    }
    std::cout << "SDL3 Dreamcast mapping / focus / hotplug policy: OK\n";
}
