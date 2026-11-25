#pragma once
#include "rayman2fix/gamepad_input_policy.hpp"
#include "rayman2fix/gamepad_control_info.hpp"
#include <filesystem>
#include <array>

namespace r2fix {
using SdlPadLogger = void (*)(const char*);
// Single-thread owner. No SDL headers/import library or native keyboard writes.
// Explicit shutdown is available for standalone smoke tests; the in-game
// instance has process lifetime and is never destroyed under loader lock.
class SdlGamepads {
public:
    SdlGamepads() noexcept = default;
    SdlGamepads(const SdlGamepads&) = delete;
    SdlGamepads& operator=(const SdlGamepads&) = delete;
    bool initialize(const std::filesystem::path& dll, SdlPadLogger logger) noexcept;
    PadSample poll(bool foreground, PadContext context) noexcept;
    void shutdown() noexcept;
    PadLabelFamily family() const noexcept { return family_; }
private:
    struct Api;
    Api* api_ = nullptr;
    struct Slot { void* pad=nullptr; bool armed=false; };
    std::array<Slot,8> slots_{};
    void* pad_ = nullptr;
    unsigned long owner_ = 0;
    unsigned long next_scan_ = 0;
    bool scan_due_ = true;
    SdlPadLogger logger_ = nullptr;
    PadLabelFamily family_ = PadLabelFamily::generic;
};
} // namespace r2fix
