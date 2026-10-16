#include "StabilityTimers.h"

#include <limits>

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

uint8_t AdvanceStableCellTimerV744825(uint8_t& timer, uint32_t& seed) noexcept {
    uint8_t remaining = timer & 31u;
    if (remaining == 31u) {
        seed = seed * 214013u + 2531011u;
        // One conversion and multiplication, not Rand01()*3: float32
        // rounding at the upper endpoint is observable in the original.
        const float scaled = static_cast<float>((seed >> 16) & 32767u) *
            9.155552834272385e-05f;
        remaining = static_cast<uint8_t>(static_cast<uint32_t>(scaled) + 3u);
    } else if (remaining != 0u) {
        --remaining;
    }
    timer = static_cast<uint8_t>((timer & 224u) | remaining);
    return remaining;
}
