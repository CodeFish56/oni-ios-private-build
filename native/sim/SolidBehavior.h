#pragma once

#include "GasPostProcess.h"

struct SolidElementV744825 {
    uint32_t hash = 0;
    uint8_t state = 0;
    float diagonalMinimumMass = 0.0f; // wire +44 -> native postprocess +36
    uint16_t sublimateIndex = 65535;
};

bool DecodeSolidElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<SolidElementV744825>& elements, std::string& error);

// Validate once for an enclosing ordered postprocess transaction. The worker
// below mutates that transaction's shadow world in place; callers discard it
// on failure. This avoids copying the complete grid for every sand cell.
bool ValidateSolidDomainV744825(const LoadedWorldV15& world,
    const std::vector<SolidElementV744825>& elements, uint16_t vacuum,
    const std::vector<uint8_t>& timers, std::string& error);
bool ProcessUnstableSolidCellInPlaceV744825(LoadedWorldV15& world,
    const std::vector<SolidElementV744825>& elements, uint16_t vacuum,
    std::size_t cell, bool diagonals, GasRandomStateV744825& random,
    std::vector<uint8_t>& timers, std::vector<uint32_t>& changes,
    std::string& error, uint16_t tickCount = 0);

// One original PostProcessCell solid branch, called in the enclosing ordered
// cell traversal. Headless (InitializeFromCells headless=true) behavior only:
// basic vertical swaps, diagonal mass splitting and fluid displacement. No
// UnstableCellInfo is emitted by this native headless branch. Notifications
// retain duplicate simulation indices; their low-five timer resets happen at
// the original mutation point, before later cells are visited.
// Disease/radiation/auxiliary disease histories are outside this contract.
// Failure leaves all arguments unchanged. Stable solid elements are no-ops.
bool ProcessUnstableSolidCellV744825(LoadedWorldV15& world,
    const std::vector<SolidElementV744825>& elements, uint16_t vacuum,
    std::size_t cell, bool diagonals, GasRandomStateV744825& random,
    std::vector<uint8_t>& timers, std::vector<uint32_t>& changes,
    std::string& error, uint16_t tickCount = 0);
