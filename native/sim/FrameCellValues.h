#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// One verified part of Build U59-744825-V's frame message processing.
// This is not a replacement for SimDLL or its world simulation.
class FrameCellValues {
public:
    static constexpr int32_t SetInsulationValueMessageId = -898773121;
    static constexpr int32_t SetStrengthValueMessageId = 1593243982;

    // Copies the managed 8-byte SetCellFloatValue payload into the frame queue.
    bool EnqueueInsulation(const void* payload, std::size_t bytes);
    bool EnqueueStrength(const void* payload, std::size_t bytes);

    // Applies queued values to the exposed cell array at the frame boundary.
    // Returns false before writing anything when any cell is out of range.
    bool ApplyInsulation(uint8_t* cells, std::size_t cellCount);
    bool ApplyStrength(uint8_t* cells, std::size_t cellCount);

    std::size_t QueuedInsulationCount() const { return insulation_.size(); }
    std::size_t QueuedStrengthCount() const { return strength_.size(); }

    static uint8_t EncodeInsulation(float value);
    static uint8_t EncodeStrength(float value);

private:
    struct SetCellFloatValue {
        int32_t cellIdx;
        float value;
    };
    static_assert(sizeof(SetCellFloatValue) == 8);

    std::vector<SetCellFloatValue> insulation_;
    std::vector<SetCellFloatValue> strength_;
    static uint8_t TruncateLowByte(float value);
    static bool ApplyQueued(std::vector<SetCellFloatValue>& messages,
                            uint8_t* cells, std::size_t cellCount,
                            uint8_t (*encode)(float));
};
