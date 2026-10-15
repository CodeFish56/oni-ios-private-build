// Generated from recovered C# and the matching Build 744825 SimDLL PDB.
// Layout contract only: this is not a simulator implementation.
#pragma once
#include <cstddef>
#include <cstdint>
#pragma pack(push, 4)
struct GameDataUpdate {
    int32_t numFramesProcessed;
    const uint16_t * elementIdx;
    const float * temperature;
    const float * mass;
    const uint8_t * properties;
    const uint8_t * insulation;
    const uint8_t * strengthInfo;
    const float * radiation;
    const uint8_t * diseaseIdx;
    const int32_t * diseaseCount;
    const uint16_t * backwallElement; // PDB: backwallElementIdx
    const float * backwallMass; // PDB: backwallMasses
    const float * backwallTemperature; // PDB: backwallTemperatures
    int32_t numSolidInfo;
    const void * solidInfo;
    int32_t numLiquidChangeInfo;
    const void * liquidChangeInfo;
    int32_t numSolidSubstanceChangeInfo;
    const void * solidSubstanceChangeInfo;
    int32_t numSubstanceChangeInfo;
    const void * substanceChangeInfo;
    int32_t numCallbackInfo;
    const void * callbackInfo;
    int32_t numSpawnFallingLiquidInfo;
    const void * spawnFallingLiquidInfo;
    int32_t numDigInfo;
    const void * digInfo;
    int32_t numSpawnOreInfo;
    const void * spawnOreInfo;
    int32_t numSpawnFXInfo;
    const void * spawnFXInfo;
    int32_t numUnstableCellInfo;
    const void * unstableCellInfo;
    int32_t numWorldDamageInfo;
    const void * worldDamageInfo;
    int32_t numBuildingTemperatures; // PDB: numBuildingTemperatureInfo
    const void * buildingTemperatures; // PDB: buildingTemperatureInfo
    int32_t numMassConsumedCallbacks;
    const void * massConsumedCallbacks;
    int32_t numMassEmittedCallbacks;
    const void * massEmittedCallbacks;
    int32_t numDiseaseConsumptionCallbacks; // PDB: numDiseaseConsumedCallbacks
    const void * diseaseConsumptionCallbacks; // PDB: diseaseConsumedCallbacks
    int32_t numComponentStateChangedMessages;
    const void * componentStateChangedMessages;
    int32_t numRemovedMassEntries;
    const void * removedMassEntries;
    int32_t numEmittedMassEntries;
    const void * emittedMassEntries;
    int32_t numElementChunkInfos;
    const void * elementChunkInfos;
    int32_t numElementChunkMeltedInfos;
    const void * elementChunkMeltedInfos;
    int32_t numBuildingOverheatInfos;
    const void * buildingOverheatInfos;
    int32_t numBuildingNoLongerOverheatedInfos;
    const void * buildingNoLongerOverheatedInfos;
    int32_t numBuildingMeltedInfos;
    const void * buildingMeltedInfos;
    int32_t numCellMeltedInfos;
    const void * cellMeltedInfos;
    int32_t numBackwallElementChangedInfos;
    const void * backwallElementChangedInfos;
    int32_t numBackwallShouldTransitionInfos;
    const void * backwallShouldTransitionInfos;
    int32_t numDiseaseEmittedInfos;
    const void * diseaseEmittedInfos;
    int32_t numDiseaseConsumedInfos;
    const void * diseaseConsumedInfos;
    int32_t numRadiationConsumedCallbacks;
    const void * radiationConsumedCallbacks;
    const float * accumulatedFlow;
    const void * propertyTextureFlow;
    const void * propertyTextureLiquid;
    const void * propertyTextureLiquidData;
    const void * propertyTextureMaterialData;
    const void * propertyTextureExposedToSunlight;
};
#pragma pack(pop)
static_assert(sizeof(GameDataUpdate) == 0x1F0);

static_assert(offsetof(GameDataUpdate, numFramesProcessed) == 0x0);
static_assert(offsetof(GameDataUpdate, elementIdx) == 0x4);
static_assert(offsetof(GameDataUpdate, temperature) == 0xC);
static_assert(offsetof(GameDataUpdate, mass) == 0x14);
static_assert(offsetof(GameDataUpdate, properties) == 0x1C);
static_assert(offsetof(GameDataUpdate, insulation) == 0x24);
static_assert(offsetof(GameDataUpdate, strengthInfo) == 0x2C);
static_assert(offsetof(GameDataUpdate, radiation) == 0x34);
static_assert(offsetof(GameDataUpdate, diseaseIdx) == 0x3C);
static_assert(offsetof(GameDataUpdate, diseaseCount) == 0x44);
static_assert(offsetof(GameDataUpdate, backwallElement) == 0x4C);
static_assert(offsetof(GameDataUpdate, backwallMass) == 0x54);
static_assert(offsetof(GameDataUpdate, backwallTemperature) == 0x5C);
static_assert(offsetof(GameDataUpdate, numSolidInfo) == 0x64);
static_assert(offsetof(GameDataUpdate, solidInfo) == 0x68);
static_assert(offsetof(GameDataUpdate, numLiquidChangeInfo) == 0x70);
static_assert(offsetof(GameDataUpdate, liquidChangeInfo) == 0x74);
static_assert(offsetof(GameDataUpdate, numSolidSubstanceChangeInfo) == 0x7C);
static_assert(offsetof(GameDataUpdate, solidSubstanceChangeInfo) == 0x80);
static_assert(offsetof(GameDataUpdate, numSubstanceChangeInfo) == 0x88);
static_assert(offsetof(GameDataUpdate, substanceChangeInfo) == 0x8C);
static_assert(offsetof(GameDataUpdate, numCallbackInfo) == 0x94);
static_assert(offsetof(GameDataUpdate, callbackInfo) == 0x98);
static_assert(offsetof(GameDataUpdate, numSpawnFallingLiquidInfo) == 0xA0);
static_assert(offsetof(GameDataUpdate, spawnFallingLiquidInfo) == 0xA4);
static_assert(offsetof(GameDataUpdate, numDigInfo) == 0xAC);
static_assert(offsetof(GameDataUpdate, digInfo) == 0xB0);
static_assert(offsetof(GameDataUpdate, numSpawnOreInfo) == 0xB8);
static_assert(offsetof(GameDataUpdate, spawnOreInfo) == 0xBC);
static_assert(offsetof(GameDataUpdate, numSpawnFXInfo) == 0xC4);
static_assert(offsetof(GameDataUpdate, spawnFXInfo) == 0xC8);
static_assert(offsetof(GameDataUpdate, numUnstableCellInfo) == 0xD0);
static_assert(offsetof(GameDataUpdate, unstableCellInfo) == 0xD4);
static_assert(offsetof(GameDataUpdate, numWorldDamageInfo) == 0xDC);
static_assert(offsetof(GameDataUpdate, worldDamageInfo) == 0xE0);
static_assert(offsetof(GameDataUpdate, numBuildingTemperatures) == 0xE8);
static_assert(offsetof(GameDataUpdate, buildingTemperatures) == 0xEC);
static_assert(offsetof(GameDataUpdate, numMassConsumedCallbacks) == 0xF4);
static_assert(offsetof(GameDataUpdate, massConsumedCallbacks) == 0xF8);
static_assert(offsetof(GameDataUpdate, numMassEmittedCallbacks) == 0x100);
static_assert(offsetof(GameDataUpdate, massEmittedCallbacks) == 0x104);
static_assert(offsetof(GameDataUpdate, numDiseaseConsumptionCallbacks) == 0x10C);
static_assert(offsetof(GameDataUpdate, diseaseConsumptionCallbacks) == 0x110);
static_assert(offsetof(GameDataUpdate, numComponentStateChangedMessages) == 0x118);
static_assert(offsetof(GameDataUpdate, componentStateChangedMessages) == 0x11C);
static_assert(offsetof(GameDataUpdate, numRemovedMassEntries) == 0x124);
static_assert(offsetof(GameDataUpdate, removedMassEntries) == 0x128);
static_assert(offsetof(GameDataUpdate, numEmittedMassEntries) == 0x130);
static_assert(offsetof(GameDataUpdate, emittedMassEntries) == 0x134);
static_assert(offsetof(GameDataUpdate, numElementChunkInfos) == 0x13C);
static_assert(offsetof(GameDataUpdate, elementChunkInfos) == 0x140);
static_assert(offsetof(GameDataUpdate, numElementChunkMeltedInfos) == 0x148);
static_assert(offsetof(GameDataUpdate, elementChunkMeltedInfos) == 0x14C);
static_assert(offsetof(GameDataUpdate, numBuildingOverheatInfos) == 0x154);
static_assert(offsetof(GameDataUpdate, buildingOverheatInfos) == 0x158);
static_assert(offsetof(GameDataUpdate, numBuildingNoLongerOverheatedInfos) == 0x160);
static_assert(offsetof(GameDataUpdate, buildingNoLongerOverheatedInfos) == 0x164);
static_assert(offsetof(GameDataUpdate, numBuildingMeltedInfos) == 0x16C);
static_assert(offsetof(GameDataUpdate, buildingMeltedInfos) == 0x170);
static_assert(offsetof(GameDataUpdate, numCellMeltedInfos) == 0x178);
static_assert(offsetof(GameDataUpdate, cellMeltedInfos) == 0x17C);
static_assert(offsetof(GameDataUpdate, numBackwallElementChangedInfos) == 0x184);
static_assert(offsetof(GameDataUpdate, backwallElementChangedInfos) == 0x188);
static_assert(offsetof(GameDataUpdate, numBackwallShouldTransitionInfos) == 0x190);
static_assert(offsetof(GameDataUpdate, backwallShouldTransitionInfos) == 0x194);
static_assert(offsetof(GameDataUpdate, numDiseaseEmittedInfos) == 0x19C);
static_assert(offsetof(GameDataUpdate, diseaseEmittedInfos) == 0x1A0);
static_assert(offsetof(GameDataUpdate, numDiseaseConsumedInfos) == 0x1A8);
static_assert(offsetof(GameDataUpdate, diseaseConsumedInfos) == 0x1AC);
static_assert(offsetof(GameDataUpdate, numRadiationConsumedCallbacks) == 0x1B4);
static_assert(offsetof(GameDataUpdate, radiationConsumedCallbacks) == 0x1B8);
static_assert(offsetof(GameDataUpdate, accumulatedFlow) == 0x1C0);
static_assert(offsetof(GameDataUpdate, propertyTextureFlow) == 0x1C8);
static_assert(offsetof(GameDataUpdate, propertyTextureLiquid) == 0x1D0);
static_assert(offsetof(GameDataUpdate, propertyTextureLiquidData) == 0x1D8);
static_assert(offsetof(GameDataUpdate, propertyTextureMaterialData) == 0x1E0);
static_assert(offsetof(GameDataUpdate, propertyTextureExposedToSunlight) == 0x1E8);
