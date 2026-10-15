#include "SaveWorldV15.h"

#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>

namespace {
constexpr std::size_t HeaderBytes = 29;
constexpr std::size_t ElementRecordBytes = 164;
constexpr std::size_t CellRecordBytes = 16;
constexpr std::size_t DiseaseRecordBytes = 8;
constexpr std::size_t BackwallRecordBytes = 12;
constexpr uint32_t BackwallVacuumHash = 0x2D39BF75;

bool HasBytes(const std::vector<uint8_t>& bytes, std::size_t at, std::size_t count) {
    return at <= bytes.size() && count <= bytes.size() - at;
}

uint16_t U16(const std::vector<uint8_t>& bytes, std::size_t at) {
    return static_cast<uint16_t>(bytes[at] | (static_cast<uint16_t>(bytes[at + 1]) << 8));
}

uint32_t U32(const std::vector<uint8_t>& bytes, std::size_t at) {
    return static_cast<uint32_t>(bytes[at]) |
           (static_cast<uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<uint32_t>(bytes[at + 3]) << 24);
}

float F32(const std::vector<uint8_t>& bytes, std::size_t at) {
    const uint32_t bits = U32(bytes, at);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

int32_t I32(const std::vector<uint8_t>& bytes, std::size_t at) {
    const uint32_t bits = U32(bytes, at);
    int32_t value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool ReadElementIds(const std::vector<uint8_t>& bytes,
                    std::unordered_map<uint32_t, uint16_t>& ids,
                    std::string& error) {
    if (!HasBytes(bytes, 0, 4)) {
        error = "Element table is shorter than its count.";
        return false;
    }
    const uint32_t count = U32(bytes, 0);
    if (count >= 65535 || count > (bytes.size() - 4) / ElementRecordBytes) {
        error = "Element table count or record length is invalid.";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const std::size_t at = 4 + static_cast<std::size_t>(i) * ElementRecordBytes;
        if (U16(bytes, at + 8) != i ||
            !ids.emplace(U32(bytes, at), static_cast<uint16_t>(i)).second) {
            error = "Element table indices or hashes are inconsistent.";
            return false;
        }
    }
    return true;
}

bool ReadDiseaseIds(const std::vector<uint8_t>& bytes,
                    std::size_t expectedElementCount,
                    std::unordered_map<uint32_t, uint8_t>& ids,
                    std::string& error) {
    if (!HasBytes(bytes, 0, 8)) {
        error = "Disease table is shorter than its header.";
        return false;
    }
    const uint32_t count = U32(bytes, 0);
    const uint32_t elementCount = U32(bytes, 4);
    if (count > 255 || elementCount != expectedElementCount) {
        error = "Disease table count or element count is inconsistent.";
        return false;
    }
    std::size_t at = 8;
    for (uint32_t i = 0; i < count; ++i) {
        if (!HasBytes(bytes, at, 4)) {
            error = "Disease name length is missing.";
            return false;
        }
        const uint32_t nameBytes = U32(bytes, at);
        at += 4;
        const std::size_t trailing = 4 + 4 + 4 * 16 + 4 +
                                     static_cast<std::size_t>(elementCount) * 29;
        if (!HasBytes(bytes, at, nameBytes) ||
            !HasBytes(bytes, at + nameBytes, trailing)) {
            error = "Disease table record exceeds the input.";
            return false;
        }
        at += nameBytes;
        if (!ids.emplace(U32(bytes, at), static_cast<uint8_t>(i)).second) {
            error = "Disease table contains a duplicate hash.";
            return false;
        }
        at += trailing;
    }
    if (at != bytes.size()) {
        error = "Disease table has unparsed trailing data.";
        return false;
    }
    return true;
}
} // namespace

bool DecodeSaveWorldV15(const std::vector<uint8_t>& save,
                        const std::vector<uint8_t>& elements,
                        const std::vector<uint8_t>& diseases,
                        LoadedWorldV15& world,
                        std::string& error) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    error.clear();
    if (!HasBytes(save, 0, HeaderBytes) ||
        std::memcmp(save.data(), "SIMSAVE\0", 8) != 0 || U32(save, 8) != 15) {
        error = "Expected a complete SIMSAVE version-15 header.";
        return false;
    }
    const uint32_t width = U32(save, 12);
    const uint32_t height = U32(save, 16);
    if (width < 3 || height < 3 || U32(save, 20) != 0 || U32(save, 24) != 0) {
        error = "Save grid dimensions or world offsets are unsupported.";
        return false;
    }
    const std::size_t cellCount = static_cast<std::size_t>(width) * height;
    if (cellCount > (save.size() - HeaderBytes) /
                        (CellRecordBytes + DiseaseRecordBytes + BackwallRecordBytes) ||
        save.size() != HeaderBytes + cellCount *
                           (CellRecordBytes + DiseaseRecordBytes + BackwallRecordBytes)) {
        error = "Version-15 save grid length does not match its dimensions.";
        return false;
    }
    std::unordered_map<uint32_t, uint16_t> elementIds;
    std::unordered_map<uint32_t, uint8_t> diseaseIds;
    if (!ReadElementIds(elements, elementIds, error) ||
        !ReadDiseaseIds(diseases, elementIds.size(), diseaseIds, error)) {
        return false;
    }
    const std::size_t diseaseStart = HeaderBytes + cellCount * CellRecordBytes;
    const std::size_t backwallStart = diseaseStart + cellCount * DiseaseRecordBytes;
    LoadedWorldV15 parsed;
    parsed.simWidth = width;
    parsed.simHeight = height;
    parsed.elementIdx.resize(cellCount);
    parsed.temperature.resize(cellCount);
    parsed.mass.resize(cellCount);
    // These bytes are absent from SIMSAVE v15. The reference LoadWorld/Start
    // snapshot has these defaults at all 98,304 game cells. Other saves may
    // require queued frame messages to reconstruct later modifications.
    parsed.properties.assign(cellCount, 0);
    parsed.insulation.assign(cellCount, 255);
    parsed.strengthInfo.assign(cellCount, 132);
    parsed.radiation.resize(cellCount);
    parsed.diseaseIdx.resize(cellCount);
    parsed.diseaseCount.resize(cellCount);
    parsed.backwallElement.resize(cellCount);
    parsed.backwallMass.resize(cellCount);
    parsed.backwallTemperature.resize(cellCount);
    for (std::size_t cell = 0; cell < cellCount; ++cell) {
        const std::size_t c = HeaderBytes + cell * CellRecordBytes;
        const std::size_t d = diseaseStart + cell * DiseaseRecordBytes;
        const std::size_t b = backwallStart + cell * BackwallRecordBytes;
        const auto element = elementIds.find(U32(save, c));
        const uint32_t diseaseHash = U32(save, d);
        const auto disease = diseaseIds.find(diseaseHash);
        const uint32_t backwallHash = U32(save, b);
        const auto backwall = elementIds.find(backwallHash);
        if (element == elementIds.end() ||
            (diseaseHash != 0 && disease == diseaseIds.end()) ||
            (backwallHash != BackwallVacuumHash && backwall == elementIds.end())) {
            error = "Save refers to an element or disease absent from the captured table.";
            return false;
        }
        parsed.elementIdx[cell] = element->second;
        parsed.temperature[cell] = F32(save, c + 4);
        parsed.mass[cell] = F32(save, c + 8);
        parsed.radiation[cell] = F32(save, c + 12);
        parsed.diseaseIdx[cell] = diseaseHash == 0 ? 255 : disease->second;
        parsed.diseaseCount[cell] = I32(save, d + 4);
        parsed.backwallElement[cell] = backwallHash == BackwallVacuumHash ? 65535 : backwall->second;
        parsed.backwallMass[cell] = F32(save, b + 4);
        parsed.backwallTemperature[cell] = F32(save, b + 8);
    }
    world = std::move(parsed);
    return true;
}
