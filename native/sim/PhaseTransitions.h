#pragma once

#include "GasPressure.h"

struct PhaseElementV744825 {
    uint32_t hash = 0;
    uint8_t state = 0;
    uint16_t lowTransition = 65535, highTransition = 65535;
    uint16_t lowOre = 65535, highOre = 65535;
    float specificHeatCapacity = 0, defaultMass = 0;
    float lowTemperature = 0, highTemperature = 0;
    float lowOreFraction = 0, highOreFraction = 0;
};

// Native SpawnOreInfo uses 4-byte packing. The original does not initialize
// byte 7; owned output always writes that padding byte as zero.
struct PhaseOreSpawnV744825 {
    int32_t cell = 0; // Game grid index, not the bordered simulation index.
    uint16_t element = 65535;
    uint8_t disease = 255, reserved = 0;
    float mass = 0, temperature = 0;
    int32_t diseaseCount = 0;
};
static_assert(sizeof(PhaseOreSpawnV744825) == 20);

bool DecodePhaseElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<PhaseElementV744825>& elements, std::string& error);

// Original DoStateTransition, headless, not forcing ore creation. Supports
// visibility-dependent freezing in the Water/Steam/Ice chain without disease, properties
// or byproduct ore. Returns false on unsupported input before changing world,
// changes or timers. transitioned is assigned only on success. Source arrays
// are not used by the native transition; only accumulated updated data changes.
// Notifications are raw SIM cell indices, preserving native duplicates. Each
// notification ORs low five stability timer bits with 31 when timers are given.
// visibility is a bordered simulation grid; nullptr means entirely invisible.
// Visible low-mass freezing requires oreEvents and appends one SpawnOreInfo,
// then clears the cell to vacuum. It is rejected if event storage is absent.
bool ApplyStateTransitionV744825(LoadedWorldV15& updated,
    const std::vector<PhaseElementV744825>& elements, uint32_t cell,
    std::vector<uint32_t>& changes, bool& transitioned, std::string& error,
    std::vector<uint8_t>* timers = nullptr,
    const std::vector<uint8_t>* visibility = nullptr,
    std::vector<PhaseOreSpawnV744825>* oreEvents = nullptr);

// Exact early exits in native DoPartialHeatTransition: no mutation and no RNG
// consumption. True establishes a native no-op; false means the more involved
// 5 kg transition/displacement path has to be implemented by the caller.
bool IsPartialHeatTransitionNoopV744825(const LoadedWorldV15& updated,
    const std::vector<PhaseElementV744825>& elements, uint32_t cell,
    uint32_t neighbour);
