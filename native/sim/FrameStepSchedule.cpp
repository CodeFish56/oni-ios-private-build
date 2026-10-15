#include "FrameStepSchedule.h"

#include <algorithm>
#include <cmath>
#include <limits>

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

bool PlanFrameSteps(float elapsedSeconds, float& carrySeconds,
                    FrameStepPlan& plan) {
    if (!std::isfinite(carrySeconds) || carrySeconds < 0.0f) {
        return false;
    }
    if (!(elapsedSeconds > 0.0f)) {
        // Original Sim::Main calls CopyUpdatedCellsToCells and
        // UpdateComponentsDataListOnly on this path, not UpdateData.
        plan = {0, carrySeconds};
        return true;
    }
    const float total = elapsedSeconds + carrySeconds;
    const float scaled = total * 5.0f;
    if (!std::isfinite(scaled) ||
        static_cast<double>(scaled) >= 2147483648.0 ||
        static_cast<double>(scaled) < -2147483648.0) {
        return false;
    }
    const int32_t updateCalls = std::max<int32_t>(1, static_cast<int32_t>(scaled));
    const float remainder = total - static_cast<float>(updateCalls) * 0.2f;
    carrySeconds = remainder > 0.0f ? remainder : 0.0f;
    plan = {updateCalls, carrySeconds};
    return true;
}
