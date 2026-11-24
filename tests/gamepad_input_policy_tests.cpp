#include "rayman2fix/gamepad_input_policy.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* msg) {
    if (!value) throw std::runtime_error(msg);
}
bool has(std::uint32_t actions, r2fix::PadAction action) {
    return (actions & r2fix::pad_action_bit(action)) != 0;
}
}

int main() {
    using namespace r2fix;
    PadTransitionGate gate;
    PadSample a{};
    a.device_id = 7;
    a.connected = true;
    a.foreground = true;
    a.south = true;

    // Missing/disconnected pads must NEVER synthesize input.
    auto e = gate.update(PadSample{});
    check(!e.eligible && e.pressed == 0 && e.held == 0, "no pad is neutral");

    e = gate.update(a);
    check(e.eligible && has(e.pressed, PadAction::primary) &&
          has(e.held, PadAction::primary), "new press generated exactly once");
    e = gate.update(a);
    check(e.pressed == 0 && e.released == 0 &&
          has(e.held, PadAction::primary), "held state has no key repeat");

    // Loss of focus releases all logical buttons even if the hardware
    // remains held. Repeated background samples never emit duplicate ups.
    a.foreground = false;
    e = gate.update(a);
    check(!e.eligible && has(e.released, PadAction::primary) &&
          e.held == 0, "focus lost releases active controls");
    e = gate.update(a);
    check(e.pressed == 0 && e.released == 0, "no repeated release");
    a.foreground = true;
    e = gate.update(a);
    check(has(e.pressed, PadAction::primary), "focus restored gets fresh edge");

    // Unplugging immediately resets state; reconnect starts fresh.
    a.connected = false;
    e = gate.update(a);
    check(has(e.released, PadAction::primary) && !e.eligible,
          "disconnect releases all");
    e = gate.update(a);
    check(e.released == 0, "disconnect releases once");
    a.connected = true;
    a.device_id = 8;
    e = gate.update(a);
    check(has(e.pressed, PadAction::primary), "reconnected new pad works");

    // Switching straight from one connected pad to another cannot inherit
    // held buttons. The new pad is accepted on its next sample.
    a.device_id = 9;
    e = gate.update(a);
    check(has(e.released, PadAction::primary) &&
          e.held == 0 && !e.eligible, "identity switch releases old pad");
    e = gate.update(a);
    check(e.eligible && has(e.pressed, PadAction::primary),
          "new identity requires a fresh sample");

    // Axis deadzone has strict, stable thresholds and no overflow at -32768.
    a.south = false;
    a.left_x = -32768;
    a.left_y = 32767;
    e = gate.update(a);
    check(has(e.held, PadAction::left) &&
          has(e.held, PadAction::down), "full-scale axes map correctly");
    a.left_x = -kPadAxisDeadzone;
    a.left_y = kPadAxisDeadzone;
    e = gate.update(a);
    check(!has(e.held, PadAction::left) &&
          !has(e.held, PadAction::down), "deadzone boundary is neutral");

    a.left_x = kPadAxisDeadzone + 1;
    a.left_y = -kPadAxisDeadzone - 1;
    e = gate.update(a);
    check(has(e.held, PadAction::right) && has(e.held, PadAction::up),
          "axes outside deadzone produce directions");
    a.dpad_left = true;
    a.dpad_down = true;
    e = gate.update(a);
    check(!has(e.held, PadAction::left) &&
          !has(e.held, PadAction::right) &&
          !has(e.held, PadAction::up) &&
          !has(e.held, PadAction::down),
          "opposing dpad and axis directions cancel");

    // Button mapping is logical only: there is no SendInput, keyboard hook,
    // native engine write, background input or SDL DLL in this phase.
    a.dpad_left = false;
    a.dpad_down = false;
    a.left_x = 0;
    a.left_y = 0;
    a.east = true;
    a.start = true;
    a.left_shoulder = true;
    a.right_shoulder = true;
    e = gate.update(a);
    check(has(e.held, PadAction::secondary) &&
          has(e.held, PadAction::pause) &&
          has(e.held, PadAction::camera_left) &&
          has(e.held, PadAction::camera_right),
          "logical button set is independent");

    // An invalid SDL instance ID cannot retain live controls.
    a.device_id = 0;
    e = gate.update(a);
    check(!e.eligible && e.held == 0 && e.released != 0,
          "invalid identity is disconnected");

    std::cout << "SDL3 gamepad focus/hotplug policy: OK\n";
    return 0;
}
