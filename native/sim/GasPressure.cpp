#include "GasPressure.h"
#include "PressureFlux.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {
uint32_t U32(const std::vector<uint8_t>& bytes, std::size_t at) {
    return static_cast<uint32_t>(bytes[at]) |
        (static_cast<uint32_t>(bytes[at + 1]) << 8) |
        (static_cast<uint32_t>(bytes[at + 2]) << 16) |
        (static_cast<uint32_t>(bytes[at + 3]) << 24);
}
float F32(const std::vector<uint8_t>& bytes, std::size_t at) {
    uint32_t bits = U32(bytes, at);
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}
} // namespace

bool DecodePressureElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<PressureElementV744825>& elements, uint16_t& vacuum,
    std::string& error) {
    error.clear();
    if (bytes.size() < 4) {
        error = "Missing element count.";
        return false;
    }
    const uint32_t count = U32(bytes, 0);
    if (count == 0 || count >= 65535 || count > (bytes.size() - 4) / 164) {
        error = "Invalid element count or record length.";
        return false;
    }
    std::vector<PressureElementV744825> parsed(count);
    uint16_t foundVacuum = 65535;
    for (uint32_t i = 0; i < count; ++i) {
        const std::size_t at = 4 + static_cast<std::size_t>(i) * 164;
        const uint32_t index = bytes[at + 8] |
            (static_cast<uint32_t>(bytes[at + 9]) << 8);
        if (index != i) {
            error = "Element indices differ from record order.";
            return false;
        }
        parsed[i].state = bytes[at + 10] & 3;
        parsed[i].flow = F32(bytes, at + 36);
        if (!std::isfinite(parsed[i].flow) || parsed[i].flow < 0.0f) {
            error = "Invalid pressure flow.";
            return false;
        }
        if (U32(bytes, at) == 0x2D39BF75)
            foundVacuum = static_cast<uint16_t>(i);
    }
    if (foundVacuum == 65535 || parsed[foundVacuum].state != 0) {
        error = "Vacuum element is missing or has an unexpected state.";
        return false;
    }
    elements = std::move(parsed);
    vacuum = foundVacuum;
    return true;
}

bool StepGasPressureV744825(const LoadedWorldV15& source,
    const std::vector<PressureElementV744825>& elements, uint16_t vacuum,
    const PressureRegionV744825& region, int direction, LoadedWorldV15& output,
    std::vector<uint32_t>& elementChanges, std::string& error,
    std::vector<std::array<float, 4>>* frameFlow) {
    error.clear();
    const std::size_t count = static_cast<std::size_t>(source.simWidth) * source.simHeight;
    if ((direction != 1 && direction != -1) || source.simWidth < 3 ||
        source.simHeight < 3 || count > std::numeric_limits<uint32_t>::max() ||
        source.elementIdx.size() != count || source.mass.size() != count ||
        source.temperature.size() != count || source.properties.size() != count ||
        source.diseaseCount.size() != count || source.diseaseIdx.size() != count ||
        vacuum >= elements.size() || elements[vacuum].state != 0 ||
        (frameFlow != nullptr && frameFlow->size() != count) ||
        region.minX > region.maxX || region.minY > region.maxY ||
        region.maxX > source.simWidth - 2 || region.maxY > source.simHeight - 2) {
        error = "Invalid pressure grid, vacuum, or iteration direction.";
        return false;
    }
    for (std::size_t cell = 0; cell < count; ++cell) {
        const uint16_t id = source.elementIdx[cell];
        if (id >= elements.size() || !std::isfinite(source.mass[cell]) ||
            source.mass[cell] < 0.0f || !std::isfinite(source.temperature[cell]) ||
            source.temperature[cell] < 0.0f || source.diseaseCount[cell] != 0 ||
            source.diseaseIdx[cell] != 255) {
            error = "Pressure pass requires valid finite grids without disease.";
            return false;
        }
        const uint8_t state = elements[id].state;
        const std::size_t x = cell % source.simWidth;
        const std::size_t y = cell / source.simWidth;
        const bool interior = x > 0 && x + 1 < source.simWidth &&
            y > 0 && y + 1 < source.simHeight;
        if (state == 2 || (state == 0 &&
            (id != vacuum || (interior && source.mass[cell] != 0.0f)))) {
            error = "Liquids and nonempty vacuum are outside this pressure pass.";
            return false;
        }
    }
    LoadedWorldV15 updated = source;
    auto flowData = frameFlow == nullptr ? std::vector<std::array<float, 4>>() : *frameFlow;
    std::vector<uint32_t> changes;
    auto Eligible = [&](std::size_t cell) {
        return (source.properties[cell] & 1) == 0 &&
            source.elementIdx[cell] == updated.elementIdx[cell] &&
            elements[source.elementIdx[cell]].state <= 1;
    };
    auto Transfer = [&](std::size_t current, std::size_t neighbor, unsigned axis) {
        if (!Eligible(current) || !Eligible(neighbor))
            return true;
        const uint16_t a = source.elementIdx[current];
        const uint16_t b = source.elementIdx[neighbor];
        const uint8_t sa = elements[a].state;
        const uint8_t sb = elements[b].state;
        if (a != b && sa == sb)
            return true;
        const float flow = sa == 1 ? elements[a].flow : elements[b].flow;
        float gain;
        if (!TryPressureMassDelta(source.mass[current], source.mass[neighbor], flow, gain))
            return false;
        if (frameFlow != nullptr && axis < 2) {
            if (axis == 0) {
                const float delta = static_cast<float>(direction) * -gain;
                flowData[current][1] += delta; flowData[neighbor][0] -= delta;
            } else {
                flowData[current][2] += -gain; flowData[neighbor][3] -= -gain;
            }
            if (!std::isfinite(flowData[current][axis == 0 ? 1 : 2]) ||
                !std::isfinite(flowData[neighbor][axis == 0 ? 0 : 3])) return false;
        }
        // Native return sign is current -> neighbor; PressureFlux uses the
        // current cell's gain. The original chooses current as donor at zero.
        const std::size_t donor = gain > 0.0f ? neighbor : current;
        const std::size_t receiver = gain > 0.0f ? current : neighbor;
        const float amount = std::min(std::abs(gain), source.mass[donor]);
        const float oldReceiverMass = updated.mass[receiver];
        const float total = oldReceiverMass + amount;
        if (!std::isfinite(total))
            return false;
        float temperature = 0.0f;
        if (total > 0.0f) {
            const float donorTemperature = updated.temperature[donor];
            const float receiverTemperature = updated.temperature[receiver];
            temperature = (receiverTemperature * oldReceiverMass +
                donorTemperature * amount) / total;
            temperature = std::max(std::min(temperature,
                std::max(donorTemperature, receiverTemperature)),
                std::min(donorTemperature, receiverTemperature));
            if (!std::isfinite(temperature))
                return false;
        }
        updated.mass[receiver] = total;
        updated.temperature[receiver] = temperature;
        updated.mass[donor] = std::max(updated.mass[donor] - amount, 0.0f);
        if (elements[source.elementIdx[receiver]].state == 0) {
            // SimData+0x1A is voidElementIdx, not vacuumElementIdx.
            // This restricted pass rejects void cells during validation.
            updated.elementIdx[receiver] = source.elementIdx[donor];
            // ChangeSubstance records a notification even for vacuum->vacuum.
            changes.push_back(static_cast<uint32_t>(receiver));
        }
        return true;
    };
    const std::size_t width = source.simWidth;
    for (uint32_t y = region.minY + 1; y < region.maxY + 1; ++y) {
        // UpdateData uses [min,max) for +1 and (min,max] for -1.
        const int left = static_cast<int>(region.minX) + 1;
        const int right = static_cast<int>(region.maxX) + 1;
        const int first = direction == 1 ? left : right;
        const int end = direction == 1 ? right : left;
        for (int x = first; x != end; x += direction) {
            const std::size_t current = static_cast<std::size_t>(y) * width + x;
            const std::size_t side = direction == 1 ? current + 1 : current - 1;
            const std::size_t above = current + width;
            const std::size_t diagonal = side + width;
            if (!Transfer(current, side, 0) || !Transfer(current, above, 1)) {
                error = "Pressure calculation overflowed.";
                return false;
            }
            // A diagonal edge is blocked when either orthogonal cell is solid.
            if ((elements[source.elementIdx[side]].state |
                 elements[source.elementIdx[above]].state) != 3 &&
                !Transfer(current, diagonal, 2)) {
                error = "Diagonal pressure calculation overflowed.";
                return false;
            }
        }
    }
    // UpdateData's second pass uses the same fixed pre-pressure source, but
    // accumulates into updatedCells. Its four pushes all write flow.w/z,
    // including horizontal pushes (original RVA 467E7 and 468A3).
    auto Displace = [&](std::size_t current, std::size_t neighbor, std::size_t destination) {
        const uint16_t gas = source.elementIdx[current];
        const uint16_t other = source.elementIdx[neighbor];
        if (other == gas || (source.properties[neighbor] & 1) != 0 ||
            elements[other].state != 1 || source.mass[current] <= source.mass[neighbor] ||
            updated.elementIdx[current] != gas || updated.elementIdx[neighbor] != other ||
            updated.mass[neighbor] <= 0.0f ||
            (updated.elementIdx[destination] != other && updated.elementIdx[destination] != vacuum) ||
            (updated.properties[destination] & 1) != 0)
            return true;
        // DoDisplacement moves the entire updated neighbor into destination,
        // combines mass/temperature, clears neighbor, and notifies both.
        const float moved = updated.mass[neighbor];
        const float oldMass = updated.mass[destination];
        const float total = oldMass + moved;
        const float a = updated.temperature[destination];
        const float b = updated.temperature[neighbor];
        float temperature = total > 0.0f ? (a * oldMass + b * moved) / total : 0.0f;
        temperature = std::max(std::min(temperature, std::max(a, b)), std::min(a, b));
        if (!std::isfinite(total) || !std::isfinite(temperature)) return false;
        updated.mass[destination] = total;
        updated.temperature[destination] = temperature;
        updated.elementIdx[destination] = other;
        updated.mass[neighbor] = 0.0f;
        updated.temperature[neighbor] = 0.0f;
        updated.elementIdx[neighbor] = vacuum;
        changes.push_back(static_cast<uint32_t>(neighbor));
        changes.push_back(static_cast<uint32_t>(destination));
        const float amount = std::min(updated.mass[current], source.mass[current] * 0.125f);
        updated.mass[neighbor] += amount;
        updated.temperature[neighbor] = source.temperature[current];
        updated.elementIdx[neighbor] = gas;
        changes.push_back(static_cast<uint32_t>(neighbor));
        updated.mass[current] = std::max(updated.mass[current] - amount, 0.0f);
        if (frameFlow != nullptr) {
            flowData[current][3] -= amount;
            flowData[neighbor][2] += amount;
            if (!std::isfinite(flowData[current][3]) || !std::isfinite(flowData[neighbor][2])) return false;
        }
        return true;
    };
    const uint32_t minX = std::max(region.minX + 1, 3u);
    const uint32_t maxX = std::min(region.maxX + 1, source.simWidth - 3);
    const uint32_t span = maxX > minX ? std::min(maxX - minX, region.maxX - region.minX) : 0;
    // The original uses MIN for the starting y (cmovge at RVA 46595), not
    // MAX as for x. Below-border neighbors are solid and reject the push.
    const uint32_t minY = std::min(region.minY + 1, 3u);
    const uint32_t maxY = std::min(region.maxY + 1, source.simHeight - 3);
    for (uint32_t y = minY; y < maxY; ++y) {
        for (uint32_t x = minX; x < minX + span; ++x) {
            const std::size_t current = std::size_t(y) * width + x;
            if (elements[source.elementIdx[current]].state != 1) continue;
            for (int64_t delta : {-static_cast<int64_t>(width), -static_cast<int64_t>(direction),
                    static_cast<int64_t>(direction), static_cast<int64_t>(width)}) {
                const auto neighbor = static_cast<std::size_t>(static_cast<int64_t>(current) + delta);
                // Avoid forming an out-of-range two-step destination when
                // the native caller has already rejected a solid neighbor.
                if (source.elementIdx[neighbor] == source.elementIdx[current] ||
                    elements[source.elementIdx[neighbor]].state != 1) continue;
                const auto destination = static_cast<std::size_t>(static_cast<int64_t>(current) + 2 * delta);
                if (destination >= count || !Displace(current, neighbor, destination)) {
                    error = "Gas displacement calculation overflowed or left the grid.";
                    return false;
                }
            }
        }
    }
    output = std::move(updated);
    if (frameFlow != nullptr) *frameFlow = std::move(flowData);
    elementChanges = std::move(changes);
    return true;
}
