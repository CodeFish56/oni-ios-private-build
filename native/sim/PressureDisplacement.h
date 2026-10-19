#pragma once
#include "LiquidFlow.h"

// Original PostProcessCell liquid branch RVA 0x4DD41..0x4E095. This is called
// only after density displacement and all four pressure-break attempts fail.
// It reads and mutates accumulated updated cells exclusively (no fixed source).
// Water is the sole supported liquid, with vacuum/gases/solids around it and
// no disease and only properties 0/0x40. No RNG or flow consumption; tick is read without advancing it. Raw substance-change
// notifications append in native order, including duplicates, and reset timers.
// A positive sub-.01 kg upper liquid can displace a neighboring gas and
// still return displaced=false; these native side effects are retained.
// Returns operation success; displaced is the native branch-completion flag:
// true means the caller must end this cell rather than run partial heat/offgas.
// Unsupported input rejects atomically; existing world, changes, timers and
// displaced are preserved. A no-op success assigns displaced=false.
bool DisplaceOverpressuredWaterV744825(LoadedWorldV15& updated,
    const std::vector<LiquidElementV744825>& elements,
    uint16_t water, uint16_t vacuum, uint32_t cell,
    std::vector<uint32_t>& changes, std::vector<uint8_t>& timers,
    bool& displaced, std::string& error, uint16_t tickCount = 0);
