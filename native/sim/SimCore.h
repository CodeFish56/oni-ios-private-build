#pragma once

#include "FrameCellValues.h"
#include "GameDataUpdate.h"
#include "SaveWorldV15.h"
#include "SimCoreExports.h"
#include "GasPostProcess.h"
#include "SimWireABI.h"
#include <array>
#include <string>
#include <vector>

// Headless native runtime for the verified Build 744825 data protocols.
// This owns real cells, saves and restricted gas or water positive-time output.
// General mixed-world physics and game capability gates remain unavailable.
class SimCore {
public:
    static constexpr uint64_t Capabilities = OniSim_HeadlessInitialization |
        OniSim_PreparedGrids | OniSim_LoadV15 | OniSim_ZeroTimeFrames |
        OniSim_CellValues | OniSim_SaveV15 | OniSim_SingleGasPhysics |
        OniSim_OxygenCO2Displacement | OniSim_LowMassGasPostprocess |
        OniSim_StabilityTimerSemantics | OniSim_RestrictedWaterPhysics |
        OniSim_RestrictedHeatExchange | OniSim_WaterGasPhysics;

    void Initialize(OniSim_GameMessageHandler callback);
    void Shutdown();
    void* HandleMessage(int32_t id, int32_t bytes, const uint8_t* data);
    void* HandleMessages(int32_t id, int32_t bytes, int32_t count, const uint8_t* data);
    uint8_t* BeginSave(int32_t* bytes, int32_t x, int32_t y);
    void EndSave();
    void GetDiagnostics(OniSimDiagnostics* state);
    int32_t CopyStabilityTimers(uint8_t* destination, int32_t length);
    uint32_t GetTickCount();
    OniSimStatus Status() const { return status_; }
    const std::string& Error() const { return error_; }
    void SetFailure(OniSimStatus status, const char* message);
    const LoadedWorldV15& InternalWorld() const { return world_; }

private:
    enum class Phase { Cold, Initialized, GridReady, Started };
    struct ElementInfo {
        uint32_t hash;
        uint8_t state, gradientCount;
        uint16_t lowTransition, highTransition;
        float lowTemperature, highTemperature;
        float lightAbsorption, lightMassScale;
        uint32_t material, colour;
        std::array<uint32_t, 6> gradient;
    };
    Phase phase_ = Phase::Cold;
    OniSimStatus status_ = OniSimStatus::Ok;
    std::string error_;
    OniSim_GameMessageHandler callback_ = nullptr;
    std::vector<uint8_t> elementBytes_, diseaseBytes_;
    std::vector<ElementInfo> elements_;
    std::vector<uint32_t> diseaseHashes_;
    uint16_t vacuum_ = 65535, boundary_ = 65535;
    uint32_t seed_ = 0;
    bool diseasesReady_ = false;
    bool radiationEnabled_ = false;
    bool worldOffsetsReady_ = false;
    bool worldDefinitionPresent_ = false;
    bool seedKnown_ = false;
    bool allocationOnly_ = false;
    LoadedWorldV15 world_, view_;
    GameDataUpdate update_{};
    FrameCellValues cellValues_;
    int32_t pendingFrames_ = 0;
    bool pendingPhysicsFrame_ = false;
    int iterateDirection_ = -1;
    float carrySeconds_ = 0;
    uint64_t physicsSteps_ = 0;
    uint16_t tickCount_ = 0;
    GasRandomStateV744825 random_{};
    struct SubstanceNotification { int32_t cell; uint16_t oldElement, newElement; };
    static_assert(sizeof(SubstanceNotification) == 8);
    std::vector<SubstanceNotification> pendingSubstance_, outputSubstance_;
    std::vector<int32_t> pendingLiquidChanges_, outputLiquidChanges_;
    std::vector<std::array<float, 4>> frameFlow_;
    std::vector<uint16_t> flowSourceElements_;
    std::vector<uint8_t> stabilityTimers_;
    std::vector<float> accumulatedFlow_;
    std::vector<uint8_t> textureFlow_, textureLiquid_, textureLiquidData_, textureMaterial_, sunlight_;
    std::vector<uint8_t> save_;
    bool saveOpen_ = false;

    bool Fail(OniSimStatus status, const char* message);
    void ClearStatus();
    bool SetElements(const uint8_t* data, std::size_t bytes);
    bool SetDiseases(const uint8_t* data, std::size_t bytes);
    bool InitializeCells(const uint8_t* data, std::size_t bytes);
    bool AllocateCells(const uint8_t* data, std::size_t bytes);
    bool Load(const uint8_t* data, std::size_t bytes);
    bool QueueFrame(const uint8_t* data, std::size_t bytes);
    bool AdvanceFluidFrame(const NewGameFrame& frame, LoadedWorldV15& world);
    void RefreshCells(bool initial);
    bool RefreshTextures();
    void RefreshSunlight();
    void RefreshFlow();
    void BindOutput();
    std::size_t CellCount() const;
};
