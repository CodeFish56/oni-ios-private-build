#pragma once

#include "SaveWorldV15.h"
#include <array>

struct PressureElementV744825 {
    uint8_t state = 0;
    float flow = 0.0f;
};

// Bounds from one NewGameFrame record, in game-grid coordinates. The upper
// bounds are exclusive for ascending iteration. Native simulation adds the
// one-cell border offset before traversing the region.
struct PressureRegionV744825 {
    uint32_t minX = 0;
    uint32_t minY = 0;
    uint32_t maxX = 0;
    uint32_t maxY = 0;
};

bool DecodePressureElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<PressureElementV744825>& elements, uint16_t& vacuum,
    std::string& error);

// Reconstructed pressure pass, not a complete UpdateData/physics step.
// Supports one gas species, vacuum, and solid obstacles, without diseases.
// direction is the already-negated SimData.iterateDirection (+1 or -1).
// The source remains fixed throughout the pass; output contains accumulated
// transfers. Substance notifications include same-element receiver writes;
// native event packing and game-grid conversion belong to the caller.
bool StepSingleGasPressureV744825(const LoadedWorldV15& source,
    const std::vector<PressureElementV744825>& elements, uint16_t vacuum,
    const PressureRegionV744825& region, int direction, LoadedWorldV15& output,
    std::vector<uint32_t>& elementChanges, std::string& error,
    std::vector<std::array<float, 4>>* frameFlow = nullptr);
