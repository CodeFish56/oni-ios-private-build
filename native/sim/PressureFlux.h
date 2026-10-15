#pragma once

// Signed mass change at cell A before element, temperature, disease and
// displacement handling. Positive means A receives mass from adjacent B.
// Recovered from Build U59-744825-V UpdatePressure at RVA 0x4AEA0.
// Returns false for values outside the finite, nonnegative domain tested here.
bool TryPressureMassDelta(float cellAMass, float cellBMass, float flow,
                          float& cellADelta) noexcept;
