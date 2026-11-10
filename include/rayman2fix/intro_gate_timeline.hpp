#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace r2fix {

// Bounded, passive event history. Retains the LATEST changes, so repeated
// short intro signals cannot consume the entire diagnostic budget before
// the natural scene handover. No game addresses or pointers are stored.
struct IntroPhaseSample {
    std::int32_t v16=0,v95=0,v96=0;
    std::uint8_t valid=0; // 1=v16 2=v95 4=v96; zero can mean unavailable
};
struct IntroGateChange {
    std::uint32_t elapsed_ms = 0;
    std::uint32_t previous = 0;
    std::uint32_t current = 0;
    std::uint8_t variable = 0;
    IntroPhaseSample phase{};
};
template <std::size_t Capacity>
struct IntroGateTimeline {
    static_assert(Capacity > 0, "At least one entry must be retained");
    std::array<IntroGateChange,Capacity> ring{};
    std::uint32_t total = 0;
    std::uint32_t var110_equal_one = 0;

    void record(std::uint32_t t, std::uint8_t variable,
                std::uint32_t previous, std::uint32_t current,
                const IntroPhaseSample& phase={}) {
        if (previous == current) return;
        ring[total % Capacity] = {t, previous, current, variable, phase};
        ++total;
        if (variable == 110 && current == 1) ++var110_equal_one;
    }
    std::size_t retained() const {
        return total < Capacity ? total : Capacity;
    }
    std::uint32_t dropped() const {
        return total - std::uint32_t(retained());
    }
    const IntroGateChange& chronological(std::size_t i) const {
        // Caller must check i < retained(). No allocation, no pointer
        // dereference, no filesystem access.
        const auto start = total >= Capacity ? total % Capacity : 0;
        return ring[(start+i)%Capacity];
    }
};
} // namespace r2fix
