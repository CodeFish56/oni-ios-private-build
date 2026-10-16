#pragma once
#include <cstdint>

#if defined(_WIN32)
#define ONI_SIM_API __declspec(dllexport)
#else
#define ONI_SIM_API __attribute__((visibility("default")))
#endif
#define ONI_SIM_NOEXCEPT noexcept

using OniSim_GameMessageHandler = int32_t (*)(int32_t messageId, void* data);

enum OniSimCapability : uint64_t {
    OniSim_HeadlessInitialization = 1ull,
    OniSim_PreparedGrids = 2ull,
    OniSim_LoadV15 = 4ull,
    OniSim_ZeroTimeFrames = 8ull,
    OniSim_CellValues = 16ull,
    OniSim_SaveV15 = 32ull,
    OniSim_SingleGasPhysics = 64ull,
    OniSim_OxygenCO2Displacement = 128ull,
    OniSim_LowMassGasPostprocess = 256ull,
    OniSim_StabilityTimerSemantics = 512ull,
    OniSim_RestrictedWaterPhysics = 1024ull,
    OniSim_WorldgenSettlement = 1ull << 16,
    OniSim_GameSimulation = 1ull << 17,
    OniSim_GameRenderData = 1ull << 18
};

enum class OniSimStatus : int32_t {
    Ok = 0,
    InvalidState = 1,
    InvalidInput = 2,
    UnsupportedMessage = 3,
    UnsupportedFeature = 4,
    OutOfMemory = 5,
    InternalError = 6
};

struct OniSimDiagnostics {
    uint32_t randomSeed;
    int32_t iterateDirection;
    int32_t displacementDirection;
    float carrySeconds;
    uint64_t physicsSteps;
};
static_assert(sizeof(OniSimDiagnostics) == 24);

// C ABI: no C++ exception crosses this boundary. Check GetLastStatus after
// every operation. Returned data is owned by the core, not the caller.
extern "C" {
ONI_SIM_API uint64_t OniSim_GetCapabilities() ONI_SIM_NOEXCEPT;
ONI_SIM_API int32_t OniSim_GetLastStatus() ONI_SIM_NOEXCEPT;
ONI_SIM_API const char* OniSim_GetLastError() ONI_SIM_NOEXCEPT;
ONI_SIM_API void OniSim_GetDiagnostics(OniSimDiagnostics* state) ONI_SIM_NOEXCEPT;
// Diagnostic copy of internal timer bytes, including the simulation border.
// nullptr/0 queries byte count. Only low five bits have defined semantics.
ONI_SIM_API int32_t OniSim_CopyStabilityTimers(uint8_t* destination, int32_t length) ONI_SIM_NOEXCEPT;
ONI_SIM_API void OniSim_Initialize(OniSim_GameMessageHandler callback) ONI_SIM_NOEXCEPT;
ONI_SIM_API void OniSim_Shutdown() ONI_SIM_NOEXCEPT;
ONI_SIM_API void* OniSim_HandleMessage(int32_t messageId, int32_t length, const uint8_t* data) ONI_SIM_NOEXCEPT;
ONI_SIM_API void* OniSim_HandleMessages(int32_t messageId, int32_t length, int32_t count, const uint8_t* data) ONI_SIM_NOEXCEPT;
ONI_SIM_API uint8_t* OniSim_BeginSave(int32_t* size, int32_t x, int32_t y) ONI_SIM_NOEXCEPT;
ONI_SIM_API void OniSim_EndSave() ONI_SIM_NOEXCEPT;
ONI_SIM_API void OniSim_DebugCrash() ONI_SIM_NOEXCEPT;
}
