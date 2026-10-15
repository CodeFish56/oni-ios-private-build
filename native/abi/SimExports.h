// Build U59-744825-V managed/native call boundary.
// Declarations only. No function body or iOS simulator is supplied here.
#pragma once
#include <cstdint>
#include "SimWireABI.h"

using GameMessageHandler = int32_t (*)(int32_t messageId, void* data);

extern "C" {
void SIM_Initialize(GameMessageHandler callback);
void SIM_Shutdown();
void* SIM_HandleMessage(int32_t messageId, int32_t length, const uint8_t* data);
void* SIM_HandleMessages(int32_t messageId, int32_t length,
                         int32_t count, const uint8_t* data);
uint8_t* SIM_BeginSave(int32_t* size, int32_t x, int32_t y);
void SIM_EndSave();
void SIM_DebugCrash();

void ConduitTemperatureManager_Initialize();
void ConduitTemperatureManager_Shutdown();
int32_t ConduitTemperatureManager_Add(float contentsTemperature,
    float contentsMass, int32_t contentsElementHash,
    int32_t conduitStructureTemperatureHandle, float conduitHeatCapacity,
    float conduitThermalConductivity, int32_t conduitInsulated);
int32_t ConduitTemperatureManager_Set(int32_t handle,
    float contentsTemperature, float contentsMass, int32_t contentsElementHash);
void ConduitTemperatureManager_Remove(int32_t handle);
const ConduitTemperatureUpdateData* ConduitTemperatureManager_Update(
    float dt, const void* buildingConductivityData);
void ConduitTemperatureManager_Clear();
}

// The audited iOS IL2CPP wrapper marshals conduitInsulated as int32_t (4-byte
// BOOL), not a C++ bool. This declaration does not supply a conduit runtime.
