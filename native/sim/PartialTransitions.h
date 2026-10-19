#pragma once

#include "GasPostProcess.h"
#include "PhaseTransitions.h"

// Build 744825 DoPartialHeatTransition (0x4B7F0), not the thermal task's full
// cell transition. Native result is separate from API success: a native false
// result can still change displaced gas cells. Supports headless, forceOre=0,
// disease-free Water/Steam/Ice/O2/CO2/vacuum and stable solid obstacles, with
// properties 0 or 0x40. No new event type is emitted in this headless domain.
// Source, flow, shared RNG seed/directions/calls are not changed. Substance
// notifications retain raw bordered SIM indices and duplicate order.
// Exact early exits inspect only required local inputs and do not clone the
// grid. Nontrivial paths validate and stage owned world/changes/timers, so
// unsupported failure returns false atomically. Native false still commits
// the original's partial effects. Moving into a border cell is unsupported.
bool ApplyPartialHeatTransitionV744825(LoadedWorldV15& updated,
    const std::vector<PhaseElementV744825>& elements, uint32_t cell,
    uint32_t neighbour, uint16_t tick, GasRandomStateV744825& random,
    std::vector<uint32_t>& changes, bool& nativeResult, std::string& error,
    std::vector<uint8_t>* timers = nullptr);

// Build 744825 DoPartialMelt (0x4BFF0). The current cell is gas/vacuum and the
// neighbour is a cold solid. Native guards include neighbour properties &0x48,
// mass strictly >5, and heat capacity calculated using the transition target.
// Same headless/domain/atomic contract as the heat-transition entry above.
bool ApplyPartialMeltV744825(LoadedWorldV15& updated,
    const std::vector<PhaseElementV744825>& elements, uint32_t cell,
    uint32_t neighbour, uint16_t tick, GasRandomStateV744825& random,
    std::vector<uint32_t>& changes, bool& nativeResult, std::string& error,
    std::vector<uint8_t>* timers = nullptr);
