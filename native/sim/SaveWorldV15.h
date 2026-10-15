#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Nine stored grid arrays plus three observed startup defaults for the
// Build U59-744825-V reference save's game-facing grid.
// The vectors include the one-cell simulation border on every side.
// This is a save decoder, not a simulation engine or a complete LoadWorld.
struct LoadedWorldV15 {
    uint32_t simWidth = 0;
    uint32_t simHeight = 0;
    std::vector<uint16_t> elementIdx;
    std::vector<float> temperature;
    std::vector<float> mass;
    std::vector<uint8_t> properties;
    std::vector<uint8_t> insulation;
    std::vector<uint8_t> strengthInfo;
    std::vector<float> radiation;
    std::vector<uint8_t> diseaseIdx;
    std::vector<int32_t> diseaseCount;
    std::vector<uint16_t> backwallElement;
    std::vector<float> backwallMass;
    std::vector<float> backwallTemperature;

    uint32_t GameWidth() const { return simWidth - 2; }
    uint32_t GameHeight() const { return simHeight - 2; }
    std::size_t SimIndex(uint32_t gameX, uint32_t gameY) const {
        return (static_cast<std::size_t>(gameY) + 1) * simWidth + gameX + 1;
    }
};

// Requires the exact captured, length-prefixed element and disease tables.
// Restricts the initial implementation to zero world offsets and a complete
// version-15 grid, which are true for the Build 744825 reference capture.
bool DecodeSaveWorldV15(const std::vector<uint8_t>& save,
                        const std::vector<uint8_t>& elements,
                        const std::vector<uint8_t>& diseases,
                        LoadedWorldV15& world,
                        std::string& error);
