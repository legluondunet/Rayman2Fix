#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace r2fix {

// Read-only, bounded timeline for the native Learn_10 director's integer
// signal V96 and physical Enter presses. Only differences are retained.
// Never reads game memory, addresses, script nodes, or saved data itself.
// The caller must use the same already-authorized game/render thread.
enum class IntroNativeSignalKind : std::uint8_t {
    signal_change=1,
    enter_press=2
};

struct IntroNativeSignalEvent {
    std::uint32_t elapsed_ms=0;
    std::int32_t signal=0;
    IntroNativeSignalKind kind=IntroNativeSignalKind::signal_change;
};

template<std::size_t Capacity>
struct IntroNativeSignalTimeline {
    static_assert(Capacity>0, "At least one signal event is retained");
    std::array<IntroNativeSignalEvent,Capacity> events{};
    std::uint32_t total=0;
    std::uint32_t signal_changes=0;
    std::uint32_t enters_104=0;
    std::uint32_t enters_110=0;
    std::uint32_t enter_presses=0;
    std::uint32_t valid_samples=0;
    std::uint32_t invalid_samples=0;
    bool signal_known=false;
    bool key_known=false;
    bool prior_key_down=false;
    std::int32_t initial_signal=0;
    std::int32_t last_signal=0;

    void record(std::uint32_t t, IntroNativeSignalKind kind, std::int32_t signal) {
        events[total%Capacity]={t,signal,kind};
        ++total;
    }

    void observe(std::uint32_t elapsed_ms,bool valid,std::int32_t signal,
                 bool enter_down) {
        // A held Enter key is ONE press, not hundreds of events.
        if (key_known && enter_down && !prior_key_down) {
            ++enter_presses;
            record(elapsed_ms,IntroNativeSignalKind::enter_press,
                   signal_known ? last_signal : 0);
        }
        key_known=true;
        prior_key_down=enter_down;
        if (!valid) {
            ++invalid_samples;
            return; // An unreadable value must not become a false transition.
        }
        ++valid_samples;
        if (!signal_known) {
            signal_known=true;
            initial_signal=last_signal=signal;
            return; // Initial state is NOT a change or a pulse.
        }
        if (signal==last_signal) return;
        last_signal=signal;
        ++signal_changes;
        if (signal==104) ++enters_104;
        if (signal==110) ++enters_110;
        record(elapsed_ms,IntroNativeSignalKind::signal_change,signal);
    }

    std::size_t retained() const {
        return total<Capacity ? total : Capacity;
    }
    std::uint32_t dropped() const {
        return total-static_cast<std::uint32_t>(retained());
    }
    const IntroNativeSignalEvent& chronological(std::size_t i) const {
        // Caller must require i < retained(). No allocations or I/O.
        const std::size_t begin=total>=Capacity ? total%Capacity : 0;
        return events[(begin+i)%Capacity];
    }
};
} // namespace r2fix
