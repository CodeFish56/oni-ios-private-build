#pragma once
#include "SaveWorldV15.h"

struct PressureBreakElementV744825 {
    uint8_t state = 0;
    float strength = 0.0f;
    float maxMass = 0.0f;
};

// Native WorldDamageInfo names the second member damageSourceOffset, but
// DoPressureBreak writes the original liquid's game-cell index into it.
struct WorldDamageInfoV744825 {
    int32_t gameCell = 0;
    int32_t damageSourceOffset = 0;
};
static_assert(sizeof(WorldDamageInfoV744825) == 8);

bool DecodePressureBreakElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<PressureBreakElementV744825>& elements, std::string& error);

// Original RVA 0x4C4E0 directional pressure test. The world, random seed,
// stability timers and flow remain unchanged. Returns operation success;
// broke is the native boolean (may be true even when border filtering omits
// notifications). Events append in near-to-far order, without deduplication.
// Supported offsets are -width,-1,+1,+width. Invalid/unobserved numeric or
// out-of-bounds inputs reject atomically and preserve events and broke.
bool DoPressureBreakV744825(const LoadedWorldV15& world,
    const std::vector<PressureBreakElementV744825>& elements,
    uint32_t cell, float pressure, int32_t offset,
    std::vector<WorldDamageInfoV744825>& events, bool& broke,
    std::string& error);

// Original PostProcessCell order: below, left, right, above, stopping after
// the first successful direction. Caller performs density displacement first.
bool CheckLiquidPressureBreakV744825(const LoadedWorldV15& world,
    const std::vector<PressureBreakElementV744825>& elements,
    uint32_t cell, std::vector<WorldDamageInfoV744825>& events, bool& broke,
    std::string& error);
