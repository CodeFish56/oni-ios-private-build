#include "GasPostProcess.h"
#include "StabilityTimers.h"
#include "PhaseTransitions.h"
#include "PressureBreak.h"
#include "SolidBehavior.h"
#include "PartialTransitions.h"
#include "PressureDisplacement.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {
uint16_t U16(const std::vector<uint8_t>& bytes, std::size_t at) {
    return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}
uint32_t U32(const std::vector<uint8_t>& bytes, std::size_t at) {
    return static_cast<uint32_t>(U16(bytes, at)) |
        (static_cast<uint32_t>(U16(bytes, at + 2)) << 16);
}
float F32(const std::vector<uint8_t>& bytes, std::size_t at) {
    const uint32_t bits = U32(bytes, at);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
float Next(GasRandomStateV744825& random) {
    random.seed = random.seed * 214013u + 2531011u;
    ++random.calls;
    // Match separate SSE conversion and multiplication; no fused arithmetic.
    return static_cast<float>((random.seed >> 16) & 32767u) *
        3.0518509447574615e-05f;
}
} // namespace

bool DecodeGasPostElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<GasPostElementV744825>& elements, std::string& error) {
    error.clear();
    if (bytes.size() < 4 || U32(bytes, 0) == 0 || U32(bytes, 0) >= 65535 ||
        U32(bytes, 0) > (bytes.size() - 4) / 164) {
        error = "Invalid gas postprocess element table.";
        return false;
    }
    std::vector<GasPostElementV744825> parsed(U32(bytes, 0));
    for (std::size_t i = 0; i < parsed.size(); ++i) {
        const std::size_t at = 4 + i * 164;
        if (U16(bytes, at + 8) != i) {
            error = "Element indices differ from record order.";
            return false;
        }
        auto& element = parsed[i];
        element.hash = U32(bytes, at);
        element.state = bytes[at + 10];
        element.molarMass = F32(bytes, at + 20);
        element.lowTemperature = F32(bytes, at + 56);
        element.highTemperature = F32(bytes, at + 60);
        element.maxMass = F32(bytes, at + 52);
        element.highTransitionIndex = U16(bytes, at + 6);
        element.sublimateIndex = U16(bytes, at + 84);
        if (!std::isfinite(element.molarMass) ||
            !std::isfinite(element.lowTemperature) ||
            !std::isfinite(element.highTemperature)) {
            error = "Invalid gas postprocess element values.";
            return false;
        }
    }
    elements = std::move(parsed);
    return true;
}

namespace {
struct PhysicalContext {
    const std::vector<PhaseElementV744825>& phases;
    const std::vector<PressureBreakElementV744825>& pressure;
    const std::vector<SolidElementV744825>& solids;
    const std::vector<LiquidElementV744825>& liquids;
    std::vector<WorldDamageInfoV744825>& damage;
    uint16_t tick;
};
bool StepPostProcessV744825(const LoadedWorldV15& source,
    const std::vector<GasPostElementV744825>& elements,
    const PressureRegionV744825& region, GasRandomStateV744825& random,
    LoadedWorldV15& output, std::vector<uint32_t>& elementChanges,
    std::string& error, std::vector<std::array<float, 4>>* frameFlow, std::vector<uint8_t>* timers,
    bool allowWater, PhysicalContext* physical = nullptr) {
    error.clear();
    const std::size_t count = static_cast<std::size_t>(source.simWidth) * source.simHeight;
    if (source.simWidth < 3 || source.simHeight < 3 ||
        source.elementIdx.size() != count || source.mass.size() != count ||
        source.temperature.size() != count || source.properties.size() != count ||
        source.diseaseIdx.size() != count || source.diseaseCount.size() != count ||
        source.insulation.size() != count || source.strengthInfo.size() != count ||
        source.radiation.size() != count || (frameFlow != nullptr && frameFlow->size() != count) ||
        (timers != nullptr && timers->size() != count) ||
        (random.displacementDirection != 1 && random.displacementDirection != -1) ||
        region.minX > region.maxX || region.minY > region.maxY ||
        region.maxX > source.simWidth - 2 || region.maxY > source.simHeight - 2) {
        error = "Invalid gas postprocess grid, region, or direction.";
        return false;
    }
    uint16_t vacuum = 65535, water = 65535;
    if (physical != nullptr && (physical->phases.size() != elements.size() ||
            physical->pressure.size() != elements.size() || physical->solids.size() != elements.size() ||
            physical->liquids.size() != elements.size() ||
            timers == nullptr || frameFlow == nullptr)) {
        error = "Physical postprocess requires matching tables, flow and timers."; return false;
    }
    for (std::size_t i = 0; i < elements.size(); ++i)
        if (elements[i].hash == 0x2D39BF75) vacuum = static_cast<uint16_t>(i);
        else if (elements[i].hash == 1836671383u) water = static_cast<uint16_t>(i);
    if (vacuum == 65535) { error = "Missing vacuum in gas postprocess."; return false; }
    if (allowWater && (water == 65535 || (elements[water].state & 3) != 2 ||
            !std::isfinite(elements[water].maxMass) || elements[water].maxMass <= 0.0f ||
            elements[water].sublimateIndex != 65535)) {
        error = "Missing or invalid Water in fluid postprocess."; return false;
    }
    float maximumGasTemperature = 0.0f;
    bool unstableSolids = false;
    float partialMeltThreshold = std::numeric_limits<float>::max();
    for (std::size_t cell = 0; cell < count; ++cell) {
        const uint16_t id = source.elementIdx[cell];
        if (id >= elements.size() || !std::isfinite(source.mass[cell]) ||
            source.mass[cell] < 0.0f || !std::isfinite(source.temperature[cell]) ||
            source.temperature[cell] < 0.0f || (source.properties[cell] & ~uint8_t(physical == nullptr ? 0 : 0x40)) != 0 ||
            source.diseaseIdx[cell] != 255 || source.diseaseCount[cell] != 0) {
            error = "Gas postprocess requires finite cells without properties or disease.";
            return false;
        }
        const auto& element = elements[id];
        const std::size_t x = cell % source.simWidth;
        const std::size_t y = cell / source.simWidth;
        const bool interior = x > 0 && x + 1 < source.simWidth &&
            y > 0 && y + 1 < source.simHeight;
        const uint8_t phase = element.state & 3;
        if (physical != nullptr) {
            unstableSolids = unstableSolids || (element.state & 11) == 11;
            const bool gas = element.hash == 0xA4E0B340 || element.hash == 0x74DC00EF || element.hash == 0xCA627A30;
            const bool solid = element.hash == 0x6D95058C || element.hash == 873952427u ||
                element.hash == 381796644u || element.hash == 1624244999u || element.hash == 493438017u ||
                element.hash == static_cast<uint32_t>(-105943486) || element.hash == static_cast<uint32_t>(-474151749);
            if (source.radiation[cell] != 0 || element.sublimateIndex != 65535 ||
                    (phase == 0 && (id != vacuum || (interior && source.mass[cell] != 0))) ||
                    (phase == 1 && !gas) || (phase == 2 && id != water) || (phase == 3 && !solid)) {
                error = "Physical postprocess requires verified non-sublimating materials without radiation."; return false;
            }
            continue;
        }
        if (allowWater && ((interior && id == vacuum && source.mass[cell] != 0.0f) ||
                source.temperature[cell] > elements[water].highTemperature + 3.0f)) {
            error = "Fluid postprocess requires empty vacuum and no partial water heat transition: cell=" +
                std::to_string(cell) + " id=" + std::to_string(id) +
                " mass=" + std::to_string(source.mass[cell]) +
                " temperature=" + std::to_string(source.temperature[cell]); return false;
        }
        if (allowWater && phase != 0 && phase != 3 &&
                (source.temperature[cell] < 275.0f || source.temperature[cell] > 360.0f)) {
            error = "Fluid postprocess requires temperatures in 275..360 K."; return false;
        }
        if (phase == 1) {
            if ((element.hash != 0xA4E0B340 && element.hash != 0x74DC00EF) ||
                element.sublimateIndex != 65535 ||
                source.temperature[cell] <= element.lowTemperature ||
                source.temperature[cell] >= element.highTemperature) {
                error = "Gas postprocess requires supported O2/CO2 away from transitions: cell=" +
                    std::to_string(cell) + " mass=" + std::to_string(source.mass[cell]) +
                    " temperature=" + std::to_string(source.temperature[cell]);
                return false;
            }
            maximumGasTemperature = std::max(maximumGasTemperature, source.temperature[cell]);
        } else if (allowWater && id == water) {
            if (source.mass[cell] > element.maxMass ||
                    source.temperature[cell] <= element.lowTemperature ||
                    source.temperature[cell] >= element.highTemperature) {
                error = "Water postprocess requires <= maxMass away from transitions."; return false;
            }
        } else if (interior && (element.hash != 0x2D39BF75 || source.mass[cell] != 0.0f) &&
                !(allowWater && element.hash == 1838482828u && element.sublimateIndex == 65535 &&
                  (element.state & 0x0b) != 0x0b &&
                  (element.highTransitionIndex == 65535 || element.highTransitionIndex == id))) {
            error = "Interior solids, liquids, and nonempty vacuum are not yet supported.";
            return false;
        } else if (!interior && element.hash != 0x2D39BF75 &&
            (element.hash != 1838482828u || element.sublimateIndex != 65535 ||
             (element.highTransitionIndex != 65535 && element.highTransitionIndex != id))) {
            error = "Unsupported border material could melt or sublimate during gas postprocess.";
            return false;
        }
        if (!interior && (element.state & 3) == 3)
            partialMeltThreshold = std::min(partialMeltThreshold, element.highTemperature + 3.0f);
        if (allowWater && phase == 3) {
            partialMeltThreshold = std::min(partialMeltThreshold, element.highTemperature + 3.0f);
            if (source.temperature[cell] > elements[water].highTemperature + 3.0f) {
                error = "Hot solid requires partial water heat transition."; return false;
            }
        }
        if (allowWater && source.radiation[cell] != 0.0f) {
            error = "Fluid postprocess does not support radiation."; return false;
        }
    }
    if (maximumGasTemperature > partialMeltThreshold) {
        error = "Gas is hot enough to require the unimplemented partial-melt path.";
        return false;
    }
    if (physical != nullptr && unstableSolids && !ValidateSolidDomainV744825(source,
            physical->solids, vacuum, *timers, error)) return false;
    LoadedWorldV15 updated = source;
    auto flowData = frameFlow == nullptr ? std::vector<std::array<float, 4>>() : *frameFlow;
    GasRandomStateV744825 nextRandom = random;
    std::vector<uint32_t> changes;
    auto timerData = timers == nullptr ? std::vector<uint8_t>() : *timers;
    auto damage = physical == nullptr ? std::vector<WorldDamageInfoV744825>() : physical->damage;
    auto Swap = [&](std::size_t a, std::size_t b) {
        std::swap(updated.elementIdx[a], updated.elementIdx[b]);
        std::swap(updated.mass[a], updated.mass[b]);
        std::swap(updated.temperature[a], updated.temperature[b]);
        // Other native swapped fields are disease state (absent here).
        // Position properties, insulation, strength, radiation stay in place.
        std::swap(updated.diseaseIdx[a], updated.diseaseIdx[b]);
        std::swap(updated.diseaseCount[a], updated.diseaseCount[b]);
    };
    const std::size_t width = source.simWidth;
    // Active regions add one to all four game-frame coordinates. Unlike the
    // pressure pass, postprocessing includes native max, then clips at the
    // selected 32-cell chunks and the simulation border.
    const auto End = [](uint32_t maximum, uint32_t dimension) {
        const uint64_t end = std::min({static_cast<uint64_t>(maximum) + 2,
            32 * ((static_cast<uint64_t>(maximum) + 31) / 32) + 1,
            static_cast<uint64_t>(dimension) - 1});
        return static_cast<uint32_t>(end);
    };
    const uint32_t endX = End(region.maxX, source.simWidth);
    const uint32_t endY = End(region.maxY, source.simHeight);
    for (uint32_t y = region.minY + 1; y < endY; ++y) {
        for (uint32_t x = region.minX + 1; x < endX; ++x) {
            const std::size_t cell = static_cast<std::size_t>(y) * width + x;
            struct ResetNotifications {
                std::vector<uint8_t>& timers;
                const std::vector<uint32_t>& changes;
                std::size_t begin;
                ~ResetNotifications() {
                    if (!timers.empty()) for (std::size_t i = begin; i < changes.size(); ++i)
                        ResetStableCellTimerV744825(timers[changes[i]]);
                }
            } reset{timerData, changes, changes.size()};
            const auto& element = elements[updated.elementIdx[cell]];
            if (physical != nullptr && (element.state & 3) == 3) {
                if (!ProcessUnstableSolidCellInPlaceV744825(updated, physical->solids, vacuum, cell, (source.savedOptions & 1) != 0,
                        nextRandom, timerData, changes, error, physical->tick)) return false;
                continue;
            }
            if (allowWater && updated.elementIdx[cell] == water) {
                if (updated.mass[cell] < 0.01f) {
                    if (frameFlow != nullptr) flowData[cell] = {};
                    updated.elementIdx[cell] = vacuum;
                    updated.mass[cell] = 0.0f; updated.temperature[cell] = 0.0f;
                    updated.diseaseIdx[cell] = 255; updated.diseaseCount[cell] = 0;
                    changes.push_back(static_cast<uint32_t>(cell));
                    continue;
                }
                const std::size_t below = cell - width;
                if (element.state == elements[updated.elementIdx[below]].state &&
                        Next(nextRandom) > 0.3f && updated.elementIdx[below] == water &&
                        updated.temperature[below] > updated.temperature[cell]) {
                    const float a = updated.temperature[cell], b = updated.temperature[below];
                    const float total = updated.mass[cell] + updated.mass[below];
                    float temperature = (updated.mass[cell] * a + updated.mass[below] * b) / total;
                    temperature = std::max(std::min(temperature, b), a);
                    updated.temperature[cell] = updated.temperature[below] = temperature;
                    // Native DoDensityDisplacement returns true after mixing;
                    // PostProcessCell ends before pressure or partial heat.
                    continue;
                }
                if (physical != nullptr) {
                    bool broke = false;
                    if (!CheckLiquidPressureBreakV744825(updated, physical->pressure,
                            static_cast<uint32_t>(cell), damage, broke, error)) return false;
                    if (broke) continue;
                    bool displaced = false;
                    if (!DisplaceOverpressuredWaterV744825(updated, physical->liquids, water, vacuum,
                            static_cast<uint32_t>(cell), changes, timerData, displaced, error, physical->tick)) return false;
                    if (displaced) continue;
                    bool heated = false;
                    for (std::size_t n : {cell - width, cell - 1, cell + 1, cell + width}) {
                        if (!ApplyPartialHeatTransitionV744825(updated, physical->phases,
                                static_cast<uint32_t>(cell), static_cast<uint32_t>(n), physical->tick,
                                nextRandom, changes, heated, error, &timerData)) return false;
                        if (heated) break;
                    }
                }
                // Mass <= maxMass rules out pressure-break and liquid pressure
                // displacement. All neighbors are <= water.highTemperature+3;
                // PartialHeatTransition exits without RNG. Water cannot offgas.
                continue;
            }
            if ((element.state & 3) != 1)
                continue;
            nextRandom.displacementDirection = -nextRandom.displacementDirection;
            const std::size_t below = cell - width;
            bool evaporate = updated.mass[cell] < 1e-9f;
            if (!evaporate && updated.mass[cell] < 0.001f) {
                const int64_t d = nextRandom.displacementDirection;
                for (int64_t delta : {-d, d, -static_cast<int64_t>(width), static_cast<int64_t>(width)}) {
                    const auto neighbor = static_cast<std::size_t>(static_cast<int64_t>(cell) + delta);
                    if (updated.mass[neighbor] >= 1.0f && (elements[updated.elementIdx[neighbor]].state & 3) == 1) {
                        evaporate = true; break;
                    }
                }
            }
            if (evaporate) {
                if (frameFlow != nullptr) flowData[cell] = {};
                updated.elementIdx[cell] = vacuum;
                updated.mass[cell] = 0.0f; updated.temperature[cell] = 0.0f;
                updated.diseaseIdx[cell] = 255; updated.diseaseCount[cell] = 0;
                // Disease infestation/error are zero throughout this domain.
                changes.push_back(static_cast<uint32_t>(cell));
                // Keep the original element reference and continue: native
                // density/random displacement may move gas back into cell.
            }
            const auto& belowElement = elements[updated.elementIdx[below]];
            bool displaced = false;
            // DoDensityDisplacement compares full states before consuming RNG.
            if (element.state == belowElement.state && Next(nextRandom) > 0.99f) {
                if (element.molarMass > belowElement.molarMass) {
                    Swap(cell, below);
                    changes.push_back(static_cast<uint32_t>(cell));
                    changes.push_back(static_cast<uint32_t>(below));
                    displaced = true;
                } else if (updated.elementIdx[cell] == updated.elementIdx[below] &&
                    updated.temperature[below] > updated.temperature[cell]) {
                    // Native vector +0x28 is temperature.begin, not mass.
                    Swap(cell, below);
                    displaced = true;
                }
            }
            bool melted = false;
            if (!displaced && physical != nullptr) {
                for (std::size_t n : {cell - width, cell - 1, cell + 1, cell + width}) {
                    if (!ApplyPartialMeltV744825(updated, physical->phases, static_cast<uint32_t>(cell),
                            static_cast<uint32_t>(n), physical->tick, nextRandom, changes, melted, error, &timerData)) return false;
                    if (melted) break;
                }
            }
            if (displaced || melted || Next(nextRandom) <= 0.9f)
                continue;
            const std::size_t candidates[] = {
                static_cast<std::size_t>(static_cast<int64_t>(cell) + nextRandom.displacementDirection),
                static_cast<std::size_t>(static_cast<int64_t>(cell) - nextRandom.displacementDirection),
                below,
            };
            std::size_t selected = count;
            float bestTemperature = std::numeric_limits<float>::max();
            for (std::size_t i = 0; i < 3; ++i) {
                const std::size_t neighbor = candidates[i];
                const float temperature = updated.temperature[neighbor];
                // Failed temperature comparison consumes no random value.
                if (temperature > bestTemperature || Next(nextRandom) <= 0.5f)
                    continue;
                const auto& neighborElement = elements[updated.elementIdx[neighbor]];
                if ((neighborElement.state & 3) != 1 ||
                    (i == 2 && element.molarMass <= neighborElement.molarMass))
                    continue;
                selected = neighbor;
                bestTemperature = temperature;
            }
            if (selected != count) {
                Swap(cell, selected);
                changes.push_back(static_cast<uint32_t>(selected));
                changes.push_back(static_cast<uint32_t>(cell));
            }
        }
    }
    if (timers != nullptr) *timers = std::move(timerData);
    if (physical != nullptr) physical->damage = std::move(damage);
    output = std::move(updated);
    if (frameFlow != nullptr) *frameFlow = std::move(flowData);
    random = nextRandom;
    elementChanges = std::move(changes);
    return true;
}
} // namespace

bool StepGasPostProcessV744825(const LoadedWorldV15& source,
    const std::vector<GasPostElementV744825>& elements,
    const PressureRegionV744825& region, GasRandomStateV744825& random,
    LoadedWorldV15& output, std::vector<uint32_t>& elementChanges,
    std::string& error, std::vector<std::array<float, 4>>* frameFlow, std::vector<uint8_t>* timers) {
    return StepPostProcessV744825(source, elements, region, random, output,
        elementChanges, error, frameFlow, timers, false);
}

bool StepFluidPostProcessV744825(const LoadedWorldV15& source,
    const std::vector<GasPostElementV744825>& elements,
    const PressureRegionV744825& region, GasRandomStateV744825& random,
    LoadedWorldV15& output, std::vector<uint32_t>& elementChanges,
    std::string& error, std::vector<std::array<float, 4>>* frameFlow, std::vector<uint8_t>* timers) {
    return StepPostProcessV744825(source, elements, region, random, output,
        elementChanges, error, frameFlow, timers, true);
}

bool StepPhysicalPostProcessV744825(const LoadedWorldV15& source,
    const std::vector<GasPostElementV744825>& elements,
    const std::vector<PhaseElementV744825>& phaseElements,
    const std::vector<PressureBreakElementV744825>& pressureElements,
    const std::vector<SolidElementV744825>& solidElements,
    const std::vector<LiquidElementV744825>& liquidElements,
    const PressureRegionV744825& region, GasRandomStateV744825& random,
    LoadedWorldV15& output, std::vector<uint32_t>& changes,
    std::vector<WorldDamageInfoV744825>& damage,
    std::vector<std::array<float, 4>>& flow, std::vector<uint8_t>& timers,
    std::string& error, uint16_t tickCount) {
    PhysicalContext context{phaseElements, pressureElements, solidElements, liquidElements, damage, tickCount};
    return StepPostProcessV744825(source, elements, region, random, output,
        changes, error, &flow, &timers, true, &context);
}
