#include "GasPostProcess.h"
#include "StabilityTimers.h"

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

bool StepGasPostProcessV744825(const LoadedWorldV15& source,
    const std::vector<GasPostElementV744825>& elements,
    const PressureRegionV744825& region, GasRandomStateV744825& random,
    LoadedWorldV15& output, std::vector<uint32_t>& elementChanges,
    std::string& error, std::vector<std::array<float, 4>>* frameFlow, std::vector<uint8_t>* timers) {
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
    uint16_t vacuum = 65535;
    for (std::size_t i = 0; i < elements.size(); ++i)
        if (elements[i].hash == 0x2D39BF75) vacuum = static_cast<uint16_t>(i);
    if (vacuum == 65535) { error = "Missing vacuum in gas postprocess."; return false; }
    float maximumGasTemperature = 0.0f;
    float partialMeltThreshold = std::numeric_limits<float>::max();
    for (std::size_t cell = 0; cell < count; ++cell) {
        const uint16_t id = source.elementIdx[cell];
        if (id >= elements.size() || !std::isfinite(source.mass[cell]) ||
            source.mass[cell] < 0.0f || !std::isfinite(source.temperature[cell]) ||
            source.temperature[cell] < 0.0f || source.properties[cell] != 0 ||
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
        } else if (interior && (element.hash != 0x2D39BF75 || source.mass[cell] != 0.0f)) {
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
    }
    if (maximumGasTemperature > partialMeltThreshold) {
        error = "Gas is hot enough to require the unimplemented partial-melt path.";
        return false;
    }
    LoadedWorldV15 updated = source;
    auto flowData = frameFlow == nullptr ? std::vector<std::array<float, 4>>() : *frameFlow;
    GasRandomStateV744825 nextRandom = random;
    std::vector<uint32_t> changes;
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
            const auto& element = elements[updated.elementIdx[cell]];
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
            if (displaced || Next(nextRandom) <= 0.9f)
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
    if (timers != nullptr) for (uint32_t n : changes) ResetStableCellTimerV744825((*timers)[n]);
    output = std::move(updated);
    if (frameFlow != nullptr) *frameFlow = std::move(flowData);
    random = nextRandom;
    elementChanges = std::move(changes);
    return true;
}
