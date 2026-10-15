#include "PressureFlux.h"

#include <algorithm>
#include <cmath>
#include <limits>

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

bool TryPressureMassDelta(float cellAMass, float cellBMass, float flow,
                          float& cellADelta) noexcept {
    if (!std::isfinite(cellAMass) || !std::isfinite(cellBMass) ||
        !std::isfinite(flow) || cellAMass < 0.0f || cellBMass < 0.0f ||
        flow < 0.0f) {
        return false;
    }

    // UpdatePressure: flow * (B - A), then SSE minss/maxss with the
    // 0.125f bounds. Keep these operations in the original order.
    const float difference = cellBMass - cellAMass;
    const float raw = flow * difference;
    const float maximumGain = cellBMass * 0.125f;
    const float maximumLoss = cellAMass * -0.125f;
    if (!std::isfinite(raw) || !std::isfinite(maximumGain) ||
        !std::isfinite(maximumLoss)) {
        return false;
    }
    cellADelta = std::max(std::min(raw, maximumGain), maximumLoss);
    return true;
}
