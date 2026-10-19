#include "PhaseTransitions.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace {
uint16_t U16(const std::vector<uint8_t>& b, std::size_t p) {
    return static_cast<uint16_t>(b[p] | (static_cast<uint16_t>(b[p + 1]) << 8));
}
uint32_t U32(const std::vector<uint8_t>& b, std::size_t p) {
    return U16(b, p) | (static_cast<uint32_t>(U16(b, p + 2)) << 16);
}
float F32(const std::vector<uint8_t>& b, std::size_t p) {
    const uint32_t bits = U32(b, p);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
}

bool DecodePhaseElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<PhaseElementV744825>& elements, std::string& error) {
    if (bytes.size() < 4) {
        error = "Phase element table is truncated.";
        return false;
    }
    const uint32_t n = U32(bytes, 0);
    if (n == 0 || n >= 65535 || n > (bytes.size() - 4) / 164) {
        error = "Phase element table has an invalid Build 744825 wire size.";
        return false;
    }
    std::vector<PhaseElementV744825> decoded(n);
    std::unordered_map<uint32_t, uint16_t> index;
    for (uint32_t i = 0; i < n; ++i) {
        const std::size_t p = 4 + static_cast<std::size_t>(i) * 164;
        if (U16(bytes, p + 8) != i || !index.emplace(U32(bytes, p), static_cast<uint16_t>(i)).second) {
            error = "Phase element indices or hashes are inconsistent.";
            return false;
        }
    }
    for (uint32_t i = 0; i < n; ++i) {
        const std::size_t p = 4 + static_cast<std::size_t>(i) * 164;
        auto& e = decoded[i];
        e.hash = U32(bytes, p);
        e.state = bytes[p + 10];
        e.lowTransition = U16(bytes, p + 4);
        e.highTransition = U16(bytes, p + 6);
        const auto low = index.find(U32(bytes, p + 68));
        const auto high = index.find(U32(bytes, p + 76));
        e.lowOre = low == index.end() ? 65535 : low->second;
        e.highOre = high == index.end() ? 65535 : high->second;
        e.lowOreFraction = F32(bytes, p + 72);
        e.highOreFraction = F32(bytes, p + 80);
        e.specificHeatCapacity = F32(bytes, p + 12);
        e.defaultMass = F32(bytes, p + 156);
        e.lowTemperature = F32(bytes, p + 56);
        e.highTemperature = F32(bytes, p + 60);
        if ((e.lowTransition != 65535 && e.lowTransition >= n) ||
            (e.highTransition != 65535 && e.highTransition >= n) ||
            !std::isfinite(e.lowTemperature) || !std::isfinite(e.highTemperature) ||
            !std::isfinite(e.specificHeatCapacity) || !std::isfinite(e.defaultMass) ||
            !std::isfinite(e.lowOreFraction) || !std::isfinite(e.highOreFraction) ||
            e.lowOreFraction < 0 || e.lowOreFraction > 1 || e.highOreFraction < 0 ||
            e.highOreFraction > 1 || e.defaultMass < 0) {
            error = "Phase table contains invalid transition indices or finite values.";
            return false;
        }
    }
    elements = std::move(decoded);
    return true;
}

bool ApplyStateTransitionV744825(LoadedWorldV15& updated,
    const std::vector<PhaseElementV744825>& elements, uint32_t cell,
    std::vector<uint32_t>& changes, bool& transitioned, std::string& error,
    std::vector<uint8_t>* timers, const std::vector<uint8_t>* visibility,
    std::vector<PhaseOreSpawnV744825>* oreEvents,
    std::vector<int32_t>* cellMeltedEvents) {
    const std::size_t n = static_cast<std::size_t>(updated.simWidth) * updated.simHeight;
    if (updated.simWidth < 3 || updated.simHeight < 3 || cell >= n ||
        updated.elementIdx.size() != n || updated.temperature.size() != n ||
        updated.mass.size() != n || updated.properties.size() != n ||
        updated.diseaseIdx.size() != n || updated.diseaseCount.size() != n ||
        (timers && timers->size() != n) || (visibility && visibility->size() != n) ||
        updated.elementIdx[cell] >= elements.size() ||
        !std::isfinite(updated.mass[cell]) || updated.mass[cell] < 0 ||
        !std::isfinite(updated.temperature[cell]) || updated.temperature[cell] < 0 ||
        updated.temperature[cell] > 10000) {
        error = "Phase transition requires a valid finite cell and timer grid.";
        return false;
    }
    const auto& e = elements[updated.elementIdx[cell]];
    const float temperature = updated.temperature[cell];
    const bool low = updated.mass[cell] > 0 && e.lowTransition != 65535 &&
        temperature < e.lowTemperature - 3.0f;
    const bool high = !low && updated.mass[cell] > 0 && e.highTransition != 65535 &&
        temperature > e.highTemperature + 3.0f;
    if (!low && !high) {
        transitioned = false;
        return true;
    }
    const uint16_t target = low ? e.lowTransition : e.highTransition;
    const uint16_t ore = low ? e.lowOre : e.highOre;
    if (target >= elements.size() || (ore != 65535 && ore >= elements.size()) ||
        (updated.properties[cell] & ~uint8_t(0x40)) != 0 ||
        updated.diseaseIdx[cell] != 255 || updated.diseaseCount[cell] != 0) {
        error = "Phase transition requires captured targets, no disease and only the optional 0x40 property.";
        return false;
    }
    const float adjusted = low ? temperature + 1.5f : temperature - 1.5f;
    const float result = adjusted > 0.0f ? adjusted : 0.0f;
    const uint32_t x = cell % updated.simWidth;
    const uint32_t y = cell / updated.simWidth;
    // Native helpers validate the flattened index only, not separate x/y.
    // Certain bordered side cells therefore alias an adjacent game row.
    const int64_t gameIndex = (int64_t(y) - 1) * (updated.simWidth - 2) + int64_t(x) - 1;
    const bool gameCell = gameIndex >= 0 && gameIndex < int64_t(updated.simWidth - 2) * (updated.simHeight - 2);
    // Native low transition tries an ore entity at <= .8 of the target's
    // default mass. SpawnOre succeeds only for a visible game cell when the
    // force-ore flag is false. The headless flag does not bypass visibility.
    const auto& te = elements[target];
    const float fraction = low ? e.lowOreFraction : e.highOreFraction;
    const float byproductMass = fraction * updated.mass[cell];
    const bool spawnByproduct = ore != 65535 && byproductMass > .001f && gameCell;
    const float remainingMass = spawnByproduct ? updated.mass[cell] - byproductMass : updated.mass[cell];
    const bool spawnOre = low && (te.state & 3) == 3 && remainingMass > 0 &&
        !(remainingMass / te.defaultMass > .8f) && gameCell &&
        visibility && (*visibility)[cell] > 0;
    const bool melted = high && (updated.properties[cell] & 0x40) != 0 && gameCell;
    if ((spawnByproduct || spawnOre) && !oreEvents) {
        error = "State transition requires owned ore event output.";
        return false;
    }
    if (melted && !cellMeltedEvents) {
        error = "High-temperature 0x40 transition requires owned CellMeltedInfo output.";
        return false;
    }
    uint16_t vacuum = 65535;
    if (spawnOre) {
        for (std::size_t i = 0; i < elements.size(); ++i)
            if (elements[i].hash == 0x2D39BF75u) vacuum = static_cast<uint16_t>(i);
        if (vacuum == 65535) {
            error = "Visible freezing requires the captured vacuum element.";
            return false;
        }
    }
    if (spawnByproduct) {
        PhaseOreSpawnV744825 event;
        event.cell = static_cast<int32_t>(gameIndex); event.element = ore;
        event.mass = byproductMass; event.temperature = result;
        oreEvents->push_back(event);
    }
    if (spawnOre) {
        PhaseOreSpawnV744825 event;
        event.cell = static_cast<int32_t>(gameIndex); event.element = target;
        event.mass = remainingMass; event.temperature = result;
        oreEvents->push_back(event);
    }
    if (melted) cellMeltedEvents->push_back(static_cast<int32_t>(gameIndex));
    if (gameCell) changes.push_back(cell);
    updated.temperature[cell] = spawnOre ? 0.0f : result;
    updated.elementIdx[cell] = spawnOre ? vacuum : target;
    updated.mass[cell] = spawnOre ? 0.0f : remainingMass;
    if (timers) (*timers)[cell] |= 31;
    transitioned = true;
    return true;
}

bool IsPartialHeatTransitionNoopV744825(const LoadedWorldV15& updated,
    const std::vector<PhaseElementV744825>& elements, uint32_t cell,
    uint32_t neighbour) {
    if (cell >= updated.mass.size() || neighbour >= updated.mass.size() ||
        cell >= updated.temperature.size() || neighbour >= updated.temperature.size() ||
        cell >= updated.elementIdx.size() || neighbour >= updated.elementIdx.size() ||
        updated.elementIdx[cell] >= elements.size() || updated.elementIdx[neighbour] >= elements.size()) return false;
    const auto& e = elements[updated.elementIdx[cell]];
    const auto& ne = elements[updated.elementIdx[neighbour]];
    const float t = updated.temperature[cell], nt = updated.temperature[neighbour];
    if (!std::isfinite(updated.mass[cell]) || !std::isfinite(t) || !std::isfinite(nt)) return false;
    return updated.mass[cell] < 5.0f || e.highTransition == 65535 ||
        nt <= e.highTemperature + 3.0f || nt < ne.lowTemperature + 13.0f ||
        t >= e.highTemperature - 3.0f;
}
