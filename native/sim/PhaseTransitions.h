#pragma once

#include "GasPressure.h"
#include <cstddef>

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
static_assert(offsetof(PhaseOreSpawnV744825, cell) == 0 &&
    offsetof(PhaseOreSpawnV744825, element) == 4 &&
    offsetof(PhaseOreSpawnV744825, disease) == 6 &&
    offsetof(PhaseOreSpawnV744825, reserved) == 7 &&
    offsetof(PhaseOreSpawnV744825, mass) == 8 &&
    offsetof(PhaseOreSpawnV744825, temperature) == 12 &&
    offsetof(PhaseOreSpawnV744825, diseaseCount) == 16);

bool DecodePhaseElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<PhaseElementV744825>& elements, std::string& error);

// Original DoStateTransition, headless, force-ore disabled for the main low-
// temperature solid product. Supports captured materials, low/high byproducts,
// and property 0x40 CellMeltedInfo, without disease or other property bits.
// Returns false on unsupported input before changing world,
// changes or timers. transitioned is assigned only on success. Source arrays
// are not used by the native transition; only accumulated updated data changes.
// Notifications are raw SIM cell indices, preserving native duplicates. Each
// notification ORs low five stability timer bits with 31 when timers are given.
// visibility is a bordered simulation grid; nullptr means entirely invisible.
// Byproduct SpawnOre calls force their own output independently of visibility.
// Visible low-mass freezing can append a second SpawnOreInfo then clear the
// cell to vacuum. All required event storage must be present before mutation.
// cellMeltedEvents uses packed four-byte signed GAME cell indices. This stream
// is emitted only for high transitions with property bit 0x40 and a valid
// native flattened game index. That property is retained by the transition.
bool ApplyStateTransitionV744825(LoadedWorldV15& updated,
    const std::vector<PhaseElementV744825>& elements, uint32_t cell,
    std::vector<uint32_t>& changes, bool& transitioned, std::string& error,
    std::vector<uint8_t>* timers = nullptr,
    const std::vector<uint8_t>* visibility = nullptr,
    std::vector<PhaseOreSpawnV744825>* oreEvents = nullptr,
    std::vector<int32_t>* cellMeltedEvents = nullptr);

// Exact early exits in native DoPartialHeatTransition: no mutation and no RNG
// consumption. True establishes a native no-op; false means the more involved
// 5 kg transition/displacement path has to be implemented by the caller.
bool IsPartialHeatTransitionNoopV744825(const LoadedWorldV15& updated,
    const std::vector<PhaseElementV744825>& elements, uint32_t cell,
    uint32_t neighbour);
