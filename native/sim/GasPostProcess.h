#pragma once

#include "GasPressure.h"

struct GasPostElementV744825 {
    uint32_t hash = 0;
    uint8_t state = 0; // Full native state byte, not just phase bits.
    float molarMass = 0.0f;
    float lowTemperature = 0.0f;
    float highTemperature = 0.0f;
    uint16_t highTransitionIndex = 65535;
    uint16_t sublimateIndex = 65535;
};

struct GasRandomStateV744825 {
    uint32_t seed = 0;
    int displacementDirection = -1;
    uint64_t calls = 0;
};

bool DecodeGasPostElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<GasPostElementV744825>& elements, std::string& error);

// Restricted PostProcessCell gas branch, after a pressure pass. Supports
// oxygen and CO2, vacuum, no interior solids or disease, including native
// low-mass evaporation; border solids must be the
// captured non-sublimating, non-melting Unobtanium. Fails rather than silently
// substituting unimplemented phase or sublimation behavior.
// Postprocessing always traverses x/y ascending, independent of pressure.
bool StepGasPostProcessV744825(const LoadedWorldV15& source,
    const std::vector<GasPostElementV744825>& elements,
    const PressureRegionV744825& region, GasRandomStateV744825& random,
    LoadedWorldV15& output, std::vector<uint32_t>& elementChanges,
    std::string& error, std::vector<std::array<float, 4>>* frameFlow = nullptr,
    std::vector<uint8_t>* timers = nullptr);
