#include "HeatExchange.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace {
uint16_t U16(const std::vector<uint8_t>& b, std::size_t p) {
    return static_cast<uint16_t>(b[p] | (static_cast<uint16_t>(b[p + 1]) << 8));
}
uint32_t U32(const std::vector<uint8_t>& b, std::size_t p) {
    return U16(b, p) | (static_cast<uint32_t>(U16(b, p + 2)) << 16);
}
float F32(const std::vector<uint8_t>& b, std::size_t p) {
    const uint32_t bits = U32(b, p);
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

constexpr uint32_t Oxygen = 0xA4E0B340;
constexpr uint32_t CarbonDioxide = 0x74DC00EF;
constexpr uint32_t Water = 0x6D796197;
constexpr uint32_t Vacuum = 0x2D39BF75;
constexpr uint32_t Unobtanium = 0x6D95058C;

bool Supported(const HeatElementV744825& e) {
    return e.hash == Oxygen || e.hash == CarbonDioxide || e.hash == Water ||
        e.hash == Vacuum || e.hash == Unobtanium;
}

// SSE MINSS/MAXSS choose the second operand for unordered comparisons. This
// matters for a zero-mass cell: the precise helper deliberately divides zero
// by zero before the equilibrium clamp. Do not replace with std::min/max.
template<class T> T NativeMin(T a, T b) { return a < b ? a : b; }
template<class T> T NativeMax(T a, T b) { return a > b ? a : b; }

bool ValidShape(const LoadedWorldV15& source, const LoadedWorldV15& updated,
    std::string& error) {
    const std::size_t n = static_cast<std::size_t>(source.simWidth) * source.simHeight;
    if (source.simWidth < 3 || source.simHeight < 3 ||
        source.elementIdx.size() != n || source.temperature.size() != n ||
        source.mass.size() != n || source.insulation.size() != n ||
        updated.simWidth != source.simWidth || updated.simHeight != source.simHeight ||
        updated.temperature.size() != n) {
        error = "Invalid heat exchange source or output grid.";
        return false;
    }
    return true;
}

bool ValidCell(const LoadedWorldV15& source,
    const std::vector<HeatElementV744825>& elements, std::size_t cell,
    const LoadedWorldV15& updated, bool task, std::string& error) {
    if (cell >= source.mass.size() || source.elementIdx[cell] >= elements.size() ||
        !std::isfinite(source.mass[cell]) || source.mass[cell] < 0 ||
        !std::isfinite(source.temperature[cell]) || source.temperature[cell] < 0 ||
        source.temperature[cell] > 10000 || !std::isfinite(updated.temperature[cell])) {
        error = "Heat exchange requires finite supported cell inputs.";
        return false;
    }
    const auto& e = elements[source.elementIdx[cell]];
    if (!Supported(e)) {
        error = "Heat exchange supports only O2, CO2, water, vacuum and Unobtanium.";
        return false;
    }
    if (e.conductivity > 0 && e.specificHeatCapacity <= 0) {
        error = "Conductive heat exchange requires positive specific heat capacity.";
        return false;
    }
    if (task && source.mass[cell] > 0 &&
        ((e.lowTransition != 65535 && updated.temperature[cell] < e.lowTemperature - 3.0f) ||
         (e.highTransition != 65535 && updated.temperature[cell] > e.highTemperature + 3.0f))) {
        error = "Heat exchange would require an unimplemented phase transition.";
        return false;
    }
    return true;
}

float AreaFor(const HeatElementV744825& a, const HeatElementV744825& b) {
    switch (b.state & 3) {
    case 1: return a.gasArea;
    case 2: return a.liquidArea;
    case 3: return a.solidArea;
    default: return 1.0f;
    }
}

void ApplyPair(const LoadedWorldV15& source,
    const std::vector<HeatElementV744825>& elements, std::size_t a, std::size_t b,
    LoadedWorldV15& updated) {
    const auto& ea = elements[source.elementIdx[a]];
    const auto& eb = elements[source.elementIdx[b]];
    const float ia = static_cast<float>(source.insulation[a]);
    const float ib = static_cast<float>(source.insulation[b]);
    const float factorA = (ia * ia) * 1.5378700481960550e-05f;
    const float factorB = (ib * ib) * 1.5378700481960550e-05f;
    const float conductivityA = factorA * ea.conductivity;
    const float conductivityB = factorB * eb.conductivity;
    float conductivity;
    if (factorA >= 1.0f && factorB >= 1.0f) {
        const float logarithm = std::log(conductivityB) + std::log(conductivityA);
        conductivity = std::exp(logarithm * 0.5f);
    } else {
        conductivity = NativeMin(conductivityA, conductivityB);
    }
    const float area = AreaFor(eb, ea) * AreaFor(ea, eb);
    conductivity = conductivity * area;
    // Each capacity multiplication is rounded to binary32 before entering the
    // original precise helper's binary64 arithmetic.
    const float capacityA32 = source.mass[a] * ea.specificHeatCapacity;
    const float capacityB32 = source.mass[b] * eb.specificHeatCapacity;
    const bool aHotter = source.temperature[a] > source.temperature[b];
    const double cold = aHotter ? source.temperature[b] : source.temperature[a];
    const double hot = aHotter ? source.temperature[a] : source.temperature[b];
    const double coldCapacity = aHotter ? capacityB32 : capacityA32;
    const double hotCapacity = aHotter ? capacityA32 : capacityB32;
    const double equilibrium = (cold * coldCapacity + hot * hotCapacity) /
        (coldCapacity + hotCapacity);
    const double difference = hot - cold;
    double exchange = (difference * static_cast<double>(conductivity)) *
        0.20000000298023223876953125;
    const double quarterDifference = difference * 0.25;
    const double coldLimit = quarterDifference * coldCapacity;
    const double hotLimit = quarterDifference * hotCapacity;
    if (coldLimit < exchange) exchange = coldLimit;
    if (hotLimit < exchange) exchange = hotLimit;
    const float coldResult = static_cast<float>(NativeMin(
        exchange / coldCapacity + cold, equilibrium));
    const float hotResult = static_cast<float>(NativeMax(
        (-1.0 / hotCapacity) * exchange + hot, equilibrium));
    const float resultA = aHotter ? hotResult : coldResult;
    const float resultB = aHotter ? coldResult : hotResult;
    updated.temperature[a] = NativeMax(NativeMin(
        (resultA - source.temperature[a]) + updated.temperature[a], 10000.0f), 1.0f);
    updated.temperature[b] = NativeMax(NativeMin(
        (resultB - source.temperature[b]) + updated.temperature[b], 10000.0f), 1.0f);
}
} // namespace

bool DecodeHeatElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<HeatElementV744825>& elements, std::string& error) {
    error.clear();
    if (bytes.size() < 4 || U32(bytes, 0) == 0 || U32(bytes, 0) >= 65535 ||
        U32(bytes, 0) > (bytes.size() - 4) / 164) {
        error = "Invalid heat exchange element table.";
        return false;
    }
    std::vector<HeatElementV744825> result(U32(bytes, 0));
    for (std::size_t i = 0; i < result.size(); ++i) {
        const std::size_t p = 4 + i * 164;
        if (U16(bytes, p + 8) != i) {
            error = "Heat exchange element indices differ from record order.";
            return false;
        }
        auto& e = result[i];
        e.hash = U32(bytes, p);
        e.state = bytes[p + 10];
        e.lowTransition = U16(bytes, p + 4);
        e.highTransition = U16(bytes, p + 6);
        e.specificHeatCapacity = F32(bytes, p + 12);
        e.conductivity = F32(bytes, p + 16);
        e.gasArea = F32(bytes, p + 32);
        e.liquidArea = F32(bytes, p + 28);
        e.solidArea = F32(bytes, p + 24);
        e.lowTemperature = F32(bytes, p + 56);
        e.highTemperature = F32(bytes, p + 60);
        if (!std::isfinite(e.specificHeatCapacity) || e.specificHeatCapacity < 0 ||
            !std::isfinite(e.conductivity) || e.conductivity < 0 ||
            !std::isfinite(e.gasArea) || e.gasArea < 0 ||
            !std::isfinite(e.liquidArea) || e.liquidArea < 0 ||
            !std::isfinite(e.solidArea) || e.solidArea < 0 ||
            !std::isfinite(e.lowTemperature) || !std::isfinite(e.highTemperature)) {
            error = "Invalid heat exchange element values.";
            return false;
        }
    }
    elements = std::move(result);
    return true;
}

bool UpdateHeatPairV744825(const LoadedWorldV15& source,
    const std::vector<HeatElementV744825>& elements, uint32_t a, uint32_t b,
    LoadedWorldV15& updated, std::string& error) {
    error.clear();
    if (!ValidShape(source, updated, error) ||
        !ValidCell(source, elements, a, updated, false, error) ||
        !ValidCell(source, elements, b, updated, false, error)) return false;
    if (elements[source.elementIdx[a]].specificHeatCapacity <= 0 ||
        elements[source.elementIdx[b]].specificHeatCapacity <= 0) {
        error = "Direct heat pairs require conductive supported elements.";
        return false;
    }
    ApplyPair(source, elements, a, b, updated);
    return true;
}

bool StepHeatExchangeV744825(const LoadedWorldV15& source,
    const std::vector<HeatElementV744825>& elements,
    const PressureRegionV744825& region, LoadedWorldV15& updated,
    std::string& error) {
    error.clear();
    if (!ValidShape(source, updated, error)) return false;
    const std::size_t count = source.mass.size();
    if (region.minX > region.maxX || region.minY > region.maxY ||
        region.maxX > source.simWidth - 2 || region.maxY > source.simHeight - 2 ||
        source.backwallMass.size() != count || source.radiation.size() != count) {
        error = "Invalid heat exchange task bounds or auxiliary grids.";
        return false;
    }
    for (std::size_t cell = 0; cell < count; ++cell) {
        if (!ValidCell(source, elements, cell, updated, true, error)) return false;
        if (source.backwallMass[cell] != 0 || source.radiation[cell] != 0) {
            error = "Heat exchange task does not yet support backwalls or radiation.";
            return false;
        }
    }
    LoadedWorldV15 next = updated;
    const std::size_t width = source.simWidth;
    const uint32_t endX = region.maxX + 1, endY = region.maxY + 1;
    auto Pair = [&](std::size_t a, std::size_t b) {
        const auto& ea = elements[source.elementIdx[a]];
        const auto& eb = elements[source.elementIdx[b]];
        if (std::abs(source.temperature[a] - source.temperature[b]) >= 1.0f &&
            ea.conductivity > 0 && eb.conductivity > 0 &&
            ((ea.state | eb.state) & 0x10) == 0) ApplyPair(source, elements, a, b, next);
    };
    for (uint32_t y = region.minY + 1; y < endY; ++y) {
        for (uint32_t x = region.minX + 1; x < endX; ++x) {
            const std::size_t cell = y * width + x;
            if (x + 1 < endX) Pair(cell, cell + 1);
            if (y + 1 < endY) Pair(cell, cell + width);
        }
    }
    // The original task performs DoStateTransition after finishing all pairs.
    // Reject an unsupported transition atomically, using the accumulated output.
    for (uint32_t y = region.minY + 1; y < endY; ++y)
        for (uint32_t x = region.minX + 1; x < endX; ++x)
            if (!ValidCell(source, elements, y * width + x, next, true, error)) return false;
    updated.temperature = std::move(next.temperature);
    return true;
}
