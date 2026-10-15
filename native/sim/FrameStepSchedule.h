#pragma once

#include <cstdint>

// One frame's physics-call count as observed in Build U59-744825-V Sim::Main.
// This only schedules calls; it does not process messages or simulate cells.
struct FrameStepPlan {
    int32_t updateDataCalls = 0;
    float remainingSeconds = 0.0f;
};

// Returns false for out-of-range inputs the original x64 conversion cannot
// represent safely on all targets. The carry is modified only on success.
bool PlanFrameSteps(float elapsedSeconds, float& carrySeconds,
                    FrameStepPlan& plan);
