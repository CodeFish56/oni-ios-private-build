#include "LiquidFlow.h"
#include "StabilityTimers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {
uint32_t U32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
float F32(const uint8_t* p) { uint32_t bits = U32(p); float value; std::memcpy(&value, &bits, 4); return value; }
bool Validate(const LoadedWorldV15& world, const std::vector<LiquidElementV744825>& elements,
    uint16_t water, uint16_t vacuum, std::string& error) {
    const std::size_t count = std::size_t(world.simWidth) * world.simHeight;
    if (world.simWidth < 3 || world.simHeight < 3 || water >= elements.size() || vacuum >= elements.size() ||
        elements[water].hash != 1836671383u || elements[vacuum].hash != 0x2D39BF75 ||
        world.elementIdx.size() != count || world.mass.size() != count || world.temperature.size() != count ||
        world.properties.size() != count || world.diseaseIdx.size() != count || world.diseaseCount.size() != count) {
        error = "Invalid water flow grid or element table."; return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const uint16_t id = world.elementIdx[i];
        if (id >= elements.size() || (id != water && id != vacuum && elements[id].hash != 0x6D95058C) ||
            !std::isfinite(world.mass[i]) || world.mass[i] < 0 || !std::isfinite(world.temperature[i]) ||
            world.temperature[i] < 0 || world.properties[i] != 0 || world.diseaseIdx[i] != 255 || world.diseaseCount[i] != 0) {
            error = "Water flow requires finite water/vacuum/Unobtanium without properties or disease."; return false;
        }
    }
    return true;
}
void Swap(LoadedWorldV15& world, std::size_t a, std::size_t b) {
    std::swap(world.elementIdx[a], world.elementIdx[b]);
    std::swap(world.mass[a], world.mass[b]); std::swap(world.temperature[a], world.temperature[b]);
    std::swap(world.diseaseIdx[a], world.diseaseIdx[b]); std::swap(world.diseaseCount[a], world.diseaseCount[b]);
}
void Clear(LoadedWorldV15& world, std::size_t cell, uint16_t vacuum) {
    world.elementIdx[cell] = vacuum; world.mass[cell] = world.temperature[cell] = 0.0f;
    world.diseaseIdx[cell] = 255; world.diseaseCount[cell] = 0;
}
bool Cell(const LoadedWorldV15& source, const std::vector<LiquidElementV744825>& elements,
    uint16_t water, uint16_t vacuum, std::size_t cell, LoadedWorldV15& updated,
    std::vector<uint32_t>& changes, std::vector<std::array<float, 4>>& flow) {
    const auto& data = elements[water];
    const std::size_t below = cell - source.simWidth;
    float remaining = source.mass[cell];
    auto Phase = [&](const LoadedWorldV15& world, std::size_t n) { return elements[world.elementIdx[n]].state & 3; };
    auto Permeable = [&](std::size_t n) { return (updated.properties[n] & 2) == 0 && Phase(updated, n) < 2; };
    auto Eligible = [&](std::size_t n) {
        return (source.elementIdx[n] == water || Phase(source, n) <= 1) &&
            Phase(updated, n) != 3 && (source.properties[n] & 2) == 0;
    };
    auto Transfer = [&](std::size_t n, float amount, unsigned component) {
        if (updated.elementIdx[n] != water && updated.elementIdx[n] != vacuum) return true;
        const float oldMass = updated.mass[n];
        const float total = oldMass + amount;
        const float a = updated.temperature[n], b = source.temperature[cell];
        float temperature = total > 0 ? (a * oldMass + b * amount) / total : 0.0f;
        temperature = std::max(std::min(temperature, std::max(a, b)), std::min(a, b));
        if (!std::isfinite(total) || !std::isfinite(temperature)) return false;
        const bool notify = updated.elementIdx[n] != water;
        updated.elementIdx[n] = water; updated.mass[n] = total; updated.temperature[n] = temperature;
        if (notify) changes.push_back(static_cast<uint32_t>(n));
        remaining -= amount; updated.mass[cell] -= amount;
        flow[cell][component] += amount;
        return std::isfinite(flow[cell][component]);
    };
    if (Phase(updated, below) != 3 && (source.properties[below] & 2) == 0) {
        if (source.elementIdx[below] != water && Phase(source, below) <= 1) {
            // Void and non-vacuum gases are rejected at the module boundary.
            const bool corner = (Permeable(cell - 1) && !Permeable(below - 1)) ||
                (Permeable(cell + 1) && !Permeable(below + 1));
            if (!corner) {
                Swap(updated, cell, below);
                changes.push_back(static_cast<uint32_t>(cell)); changes.push_back(static_cast<uint32_t>(below));
            }
            // Headless SpawnFallingLiquid returns false without a side effect.
            // A corner still returns here; it must not fall back to SwapCells.
            return true;
        }
        if (source.elementIdx[below] == water) {
            const float capacity = std::max(remaining * 1.01f, data.maxMass);
            const float amount = std::min(std::min(data.viscosity, remaining),
                std::max(capacity - source.mass[below], 0.0f) * 0.5f);
            if (!(remaining > data.minVerticalFlow && amount < data.minVerticalFlow) && amount > 0 &&
                !Transfer(below, amount, 2)) return false;
        }
    }
    for (int64_t delta : {-int64_t(1), int64_t(1)}) {
        if (!(remaining > 0)) break;
        const auto n = static_cast<std::size_t>(static_cast<int64_t>(cell) + delta);
        if (!Eligible(n)) continue;
        const float neighborMass = Phase(source, n) == 2 ? source.mass[n] : 0.0f;
        const float amount = std::min(std::min(data.viscosity, remaining), (remaining - neighborMass) * 0.25f);
        // Side falling-particle attempts fail in headless mode and use this
        // same helper fallback. Unlike vertical, the threshold is inclusive.
        if (amount >= data.minHorizontalFlow && amount > 0 && !Transfer(n, amount, delta < 0 ? 0 : 1)) return false;
    }
    const std::size_t above = cell + source.simWidth;
    if (remaining > 0 && Eligible(above)) {
        const float neighborMass = Phase(source, above) == 2 ? source.mass[above] : 0.0f;
        const float threshold = std::max(1.01f * neighborMass, data.maxMass);
        float amount = std::max(remaining - threshold, 0.0f) * 0.5f;
        if (remaining < threshold + threshold) amount = std::min(std::min(data.viscosity, remaining), amount);
        if (amount > 0.01f && !Transfer(above, amount, 3)) return false;
    }
    return true;
}
uint32_t End(uint32_t maximum, uint32_t dimension) {
    return static_cast<uint32_t>(std::min({uint64_t(maximum) + 2, 32 * ((uint64_t(maximum) + 31) / 32) + 1,
        uint64_t(dimension) - 1}));
}
} // namespace

bool DecodeLiquidElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<LiquidElementV744825>& elements, uint16_t& water, uint16_t& vacuum, std::string& error) {
    error.clear();
    if (bytes.size() < 4 || U32(bytes.data()) == 0 || U32(bytes.data()) >= 65535 ||
        U32(bytes.data()) > (bytes.size() - 4) / 164) { error = "Invalid liquid table."; return false; }
    std::vector<LiquidElementV744825> parsed(U32(bytes.data()));
    uint16_t w = 65535, v = 65535;
    for (std::size_t i = 0; i < parsed.size(); ++i) {
        const uint8_t* p = bytes.data() + 4 + i * 164;
        if (uint32_t(p[8] | uint16_t(p[9]) << 8) != i) { error = "Liquid table index mismatch."; return false; }
        auto& e = parsed[i]; e.hash = U32(p); e.state = p[10]; e.viscosity = F32(p + 40);
        e.minHorizontalFlow = F32(p + 44); e.minVerticalFlow = F32(p + 48); e.maxMass = F32(p + 52);
        if (e.hash == 1836671383u) w = static_cast<uint16_t>(i);
        if (e.hash == 0x2D39BF75) v = static_cast<uint16_t>(i);
    }
    if (w == 65535 || v == 65535 || (parsed[w].state & 3) != 2 || (parsed[v].state & 3) != 0 ||
        !std::isfinite(parsed[w].viscosity) || parsed[w].viscosity < 0 ||
        !std::isfinite(parsed[w].minHorizontalFlow) || parsed[w].minHorizontalFlow < 0 ||
        !std::isfinite(parsed[w].minVerticalFlow) || parsed[w].minVerticalFlow < 0 ||
        !std::isfinite(parsed[w].maxMass) || parsed[w].maxMass <= 0) { error = "Unsupported water/vacuum table."; return false; }
    elements = std::move(parsed); water = w; vacuum = v; return true;
}

bool UpdateWaterCellV744825(const LoadedWorldV15& source, const std::vector<LiquidElementV744825>& elements,
    uint16_t water, uint16_t vacuum, uint32_t cell, LoadedWorldV15& updated,
    std::vector<uint32_t>& changes, std::vector<std::array<float, 4>>& flow, std::string& error,
    std::vector<uint8_t>* timers) {
    error.clear();
    if (!Validate(source, elements, water, vacuum, error) || !Validate(updated, elements, water, vacuum, error)) return false;
    if (source.simWidth != updated.simWidth || source.simHeight != updated.simHeight || flow.size() != source.mass.size() ||
        cell >= source.mass.size() || cell % source.simWidth == 0 || cell % source.simWidth + 1 == source.simWidth ||
        cell / source.simWidth == 0 || cell / source.simWidth + 1 == source.simHeight ||
        source.elementIdx[cell] != water || source.mass[cell] <= 0 ||
        (timers != nullptr && timers->size() != source.mass.size())) { error = "Invalid UpdateLiquid target."; return false; }
    auto next = updated; auto nextFlow = flow; std::vector<uint32_t> notifications;
    if (!Cell(source, elements, water, vacuum, cell, next, notifications, nextFlow)) { error = "Water transfer overflowed."; return false; }
    if (timers != nullptr) for (uint32_t n : notifications) ResetStableCellTimerV744825((*timers)[n]);
    updated = std::move(next); flow = std::move(nextFlow); changes = std::move(notifications); return true;
}

bool StepWaterFlowV744825(const LoadedWorldV15& source, const std::vector<LiquidElementV744825>& elements,
    uint16_t water, uint16_t vacuum, const PressureRegionV744825& region, LoadedWorldV15& output,
    std::vector<uint32_t>& changes, std::vector<std::array<float, 4>>& flow, std::string& error) {
    error.clear();
    if (!Validate(source, elements, water, vacuum, error)) return false;
    if (flow.size() != source.mass.size() || region.minX > region.maxX || region.minY > region.maxY ||
        region.maxX > source.simWidth - 2 || region.maxY > source.simHeight - 2) { error = "Invalid liquid region/flow."; return false; }
    auto next = source; auto nextFlow = flow; std::vector<uint32_t> notifications;
    for (uint32_t y = region.minY + 1; y < region.maxY + 1; ++y) for (uint32_t x = region.minX + 1; x < region.maxX + 1; ++x) {
        const std::size_t cell = std::size_t(y) * source.simWidth + x;
        if (source.elementIdx[cell] == water && source.mass[cell] > 0 &&
            !Cell(source, elements, water, vacuum, cell, next, notifications, nextFlow)) { error = "Water transfer overflowed."; return false; }
    }
    output = std::move(next); flow = std::move(nextFlow); changes = std::move(notifications); return true;
}

bool StepWaterPostProcessV744825(const LoadedWorldV15& source, const std::vector<LiquidElementV744825>& elements,
    const std::vector<GasPostElementV744825>& post, uint16_t water, uint16_t vacuum,
    const PressureRegionV744825& region, GasRandomStateV744825& random, LoadedWorldV15& output,
    std::vector<uint32_t>& changes, std::vector<std::array<float, 4>>& flow, std::string& error) {
    error.clear();
    if (!Validate(source, elements, water, vacuum, error)) return false;
    if (post.size() != elements.size() || post[water].hash != elements[water].hash ||
        post[water].sublimateIndex != 65535 || post[vacuum].hash != elements[vacuum].hash ||
        post[vacuum].sublimateIndex != 65535 || flow.size() != source.mass.size() ||
        region.minX > region.maxX || region.minY > region.maxY || region.maxX > source.simWidth - 2 || region.maxY > source.simHeight - 2) {
        error = "Invalid water postprocess table, flow or region."; return false;
    }
    for (std::size_t cell = 0; cell < source.mass.size(); ++cell) {
        if (source.temperature[cell] > post[water].highTemperature + 3.0f ||
            (source.elementIdx[cell] == water && (source.mass[cell] > elements[water].maxMass ||
            source.temperature[cell] <= post[water].lowTemperature || source.temperature[cell] >= post[water].highTemperature)) ||
            (elements[source.elementIdx[cell]].hash == 0x6D95058C &&
                (post[source.elementIdx[cell]].hash != 0x6D95058C || post[source.elementIdx[cell]].sublimateIndex != 65535 ||
                 (post[source.elementIdx[cell]].state & 0x0b) == 0x0b))) {
            error = "Water postprocess requires <= maxMass away from transitions and stable Unobtanium."; return false;
        }
    }
    auto next = source; auto nextFlow = flow; auto nextRandom = random; std::vector<uint32_t> notifications;
    for (uint32_t y = region.minY + 1; y < End(region.maxY, source.simHeight); ++y) for (uint32_t x = region.minX + 1; x < End(region.maxX, source.simWidth); ++x) {
        const std::size_t cell = std::size_t(y) * source.simWidth + x, below = cell - source.simWidth;
        if (next.elementIdx[cell] != water) continue;
        if (next.mass[cell] < 0.01f) {
            nextFlow[cell] = {}; Clear(next, cell, vacuum); notifications.push_back(static_cast<uint32_t>(cell)); continue;
        }
        if (post[next.elementIdx[below]].state == post[water].state) {
            nextRandom.seed = nextRandom.seed * 214013u + 2531011u; ++nextRandom.calls;
            const float r = static_cast<float>((nextRandom.seed >> 16) & 32767u) * 3.0518509447574615e-05f;
            if (r > 0.3f && next.elementIdx[below] == water && next.temperature[below] > next.temperature[cell]) {
                const float a = next.temperature[cell], b = next.temperature[below];
                const float total = next.mass[cell] + next.mass[below];
                float temperature = (next.mass[cell] * a + next.mass[below] * b) / total;
                temperature = std::max(std::min(temperature, b), a);
                next.temperature[cell] = next.temperature[below] = temperature;
            }
        }
        // PartialHeatTransition needs neighbor T > water.highTemperature+3;
        // no allowed material reaches that threshold. Water has no offgas.
    }
    output = std::move(next); flow = std::move(nextFlow); random = nextRandom; changes = std::move(notifications); return true;
}
