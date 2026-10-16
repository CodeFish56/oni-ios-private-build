#pragma once

#include "GasPostProcess.h"

struct LiquidElementV744825 {
    uint32_t hash = 0;
    uint8_t state = 0;
    float viscosity = 0, minHorizontalFlow = 0, minVerticalFlow = 0, maxMass = 0;
};

bool DecodeLiquidElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<LiquidElementV744825>& elements, uint16_t& water,
    uint16_t& vacuum, std::string& error);

// Controlled headless UpdateLiquid reconstruction: one water species,
// vacuum, O2/CO2 and nonreactive Unobtanium obstacles, no properties or disease.
// These are flow operations, not general liquid/world physics. The single
// cell entry permits defined differences between the fixed source and output.
bool UpdateWaterCellV744825(const LoadedWorldV15& source,
    const std::vector<LiquidElementV744825>& elements, uint16_t water, uint16_t vacuum,
    uint32_t cell, LoadedWorldV15& updated, std::vector<uint32_t>& changes,
    std::vector<std::array<float, 4>>& flow, std::string& error,
    std::vector<uint8_t>* timers = nullptr, uint16_t tickCount = 0);

bool StepWaterFlowV744825(const LoadedWorldV15& source,
    const std::vector<LiquidElementV744825>& elements, uint16_t water, uint16_t vacuum,
    const PressureRegionV744825& region, LoadedWorldV15& output,
    std::vector<uint32_t>& changes, std::vector<std::array<float, 4>>& flow,
    std::string& error, uint16_t tickCount = 0);

// Narrow water PostProcessCell branch: <= maxMass after flow, temperatures
// away from phase changes; no pressure break, offgas, phase change or disease.
bool StepWaterPostProcessV744825(const LoadedWorldV15& source,
    const std::vector<LiquidElementV744825>& elements,
    const std::vector<GasPostElementV744825>& postElements,
    uint16_t water, uint16_t vacuum, const PressureRegionV744825& region,
    GasRandomStateV744825& random, LoadedWorldV15& output,
    std::vector<uint32_t>& changes, std::vector<std::array<float, 4>>& flow,
    std::string& error);
