#pragma once

#include <cstdint>

// Build 744825 Timers.stableCellTicks is the low five bits. The other bits
// are not initialized by the original allocation and have no known meaning.
// Only the low bits are compared for fresh worlds; operations preserve all
// upper bits when the caller supplies a defined timer byte.
inline void ResetStableCellTimerV744825(uint8_t& timer) noexcept {
    timer = static_cast<uint8_t>(timer | 31u);
}

// Original GetStableTicksRemaining RVA 284D0. This is called by unstable
// solid checks, not by every gas/liquid cell. A reset marker consumes exactly
// one random draw; all other values consume none.
uint8_t AdvanceStableCellTimerV744825(uint8_t& timer, uint32_t& seed) noexcept;
