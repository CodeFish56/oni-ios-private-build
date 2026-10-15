// Build U59-744825-V managed/native wire layouts, checked against matching PDB.
// This header is an ABI contract, not a simulation implementation.
#pragma once
#include <cstddef>
#include <cstdint>

#pragma pack(push, 4)
struct SimWireRegion {
    int32_t minX;
    int32_t minY;
    int32_t maxX;
    int32_t maxY;
};

struct NewGameFrame {
    float elapsedSeconds;
    SimWireRegion activeRegion;
    float currentSunlightIntensity;
    float currentCosmicRadiationIntensity;
};

struct ConduitTemperatureUpdateData {
    int32_t numEntries;
    const float* temperatures;
    int32_t numFrozenHandles;
    const int32_t* frozenHandles;
    int32_t numMeltedHandles;
    const int32_t* meltedHandles;
};
#pragma pack(pop)

static_assert(sizeof(void*) == 8, "Build 744825 native ABI requires 64-bit pointers");
static_assert(sizeof(SimWireRegion) == 16);
static_assert(sizeof(NewGameFrame) == 28);
static_assert(offsetof(NewGameFrame, elapsedSeconds) == 0);
static_assert(offsetof(NewGameFrame, activeRegion) == 4);
static_assert(offsetof(NewGameFrame, currentSunlightIntensity) == 20);
static_assert(offsetof(NewGameFrame, currentCosmicRadiationIntensity) == 24);
static_assert(sizeof(ConduitTemperatureUpdateData) == 36);
static_assert(offsetof(ConduitTemperatureUpdateData, temperatures) == 4);
static_assert(offsetof(ConduitTemperatureUpdateData, frozenHandles) == 16);
static_assert(offsetof(ConduitTemperatureUpdateData, meltedHandles) == 28);
