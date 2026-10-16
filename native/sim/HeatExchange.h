#pragma once

#include "GasPressure.h"

struct HeatElementV744825 {
    uint32_t hash = 0;
    uint8_t state = 0;
    uint16_t lowTransition = 65535, highTransition = 65535;
    float specificHeatCapacity = 0, conductivity = 0;
    float gasArea = 1, liquidArea = 1, solidArea = 1;
    float lowTemperature = 0, highTemperature = 0;
};

bool DecodeHeatElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<HeatElementV744825>& elements, std::string& error);

// Original UpdateTemperature entry, without the task's temperature-difference
// or conductivity prefilter. Only updated temperatures are changed. Source is
// fixed; accumulated output may already contain temperature deltas.
bool UpdateHeatPairV744825(const LoadedWorldV15& source,
    const std::vector<HeatElementV744825>& elements, uint32_t a, uint32_t b,
    LoadedWorldV15& updated, std::string& error);

// One native temperature task over game-grid bounds, upper bounds exclusive.
// Visits each cell's right pair, then upper pair; phase-changing inputs,
// backwalls and radiation are rejected before any output is changed.
bool StepHeatExchangeV744825(const LoadedWorldV15& source,
    const std::vector<HeatElementV744825>& elements,
    const PressureRegionV744825& region, LoadedWorldV15& updated,
    std::string& error);
