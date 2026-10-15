#include "FrameCellValues.h"

#include <cmath>
#include <cstring>

bool FrameCellValues::EnqueueInsulation(const void* payload, std::size_t bytes) {
    if (payload == nullptr || bytes != sizeof(SetCellFloatValue)) {
        return false;
    }
    SetCellFloatValue message{};
    std::memcpy(&message, payload, sizeof(message));
    insulation_.push_back(message);
    return true;
}

bool FrameCellValues::EnqueueStrength(const void* payload, std::size_t bytes) {
    if (payload == nullptr || bytes != sizeof(SetCellFloatValue)) {
        return false;
    }
    SetCellFloatValue message{};
    std::memcpy(&message, payload, sizeof(message));
    strength_.push_back(message);
    return true;
}

uint8_t FrameCellValues::TruncateLowByte(float value) {
    if (!std::isfinite(value) ||
        static_cast<double>(value) < -2147483648.0 ||
        static_cast<double>(value) >= 2147483648.0) {
        return 0; // x86 cvttss2si yields INT_MIN; its low byte is zero.
    }
    return static_cast<uint8_t>(static_cast<int32_t>(value));
}

uint8_t FrameCellValues::EncodeInsulation(float value) {
    // Original x64 ProcessFrame at RVA 0x1F791 multiplies by 255.0f.
    return TruncateLowByte(value * 255.0f);
}

uint8_t FrameCellValues::EncodeStrength(float value) {
    // Original x64 ProcessFrame at RVA 0x1F83A converts without scaling.
    return TruncateLowByte(value);
}

bool FrameCellValues::ApplyQueued(std::vector<SetCellFloatValue>& messages,
                                  uint8_t* cells, std::size_t cellCount,
                                  uint8_t (*encode)(float)) {
    if (cells == nullptr) {
        return false;
    }
    for (const SetCellFloatValue& message : messages) {
        if (message.cellIdx < 0 ||
            static_cast<std::size_t>(message.cellIdx) >= cellCount) {
            return false;
        }
    }
    for (const SetCellFloatValue& message : messages) {
        cells[message.cellIdx] = encode(message.value);
    }
    messages.clear();
    return true;
}

bool FrameCellValues::ApplyInsulation(uint8_t* cells, std::size_t cellCount) {
    return ApplyQueued(insulation_, cells, cellCount, EncodeInsulation);
}

bool FrameCellValues::ApplyStrength(uint8_t* cells, std::size_t cellCount) {
    return ApplyQueued(strength_, cells, cellCount, EncodeStrength);
}
