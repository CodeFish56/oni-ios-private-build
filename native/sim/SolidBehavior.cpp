#include "SolidBehavior.h"
#include "StabilityTimers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {
uint16_t U16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t U32(const uint8_t* p) { return uint32_t(U16(p)) | uint32_t(U16(p + 2)) << 16; }
float F32(const uint8_t* p) { const uint32_t bits = U32(p); float value; std::memcpy(&value, &bits, 4); return value; }

struct Context {
    LoadedWorldV15& w;
    const std::vector<SolidElementV744825>& e;
    uint16_t vacuum;
    std::vector<uint8_t>& timers;
    std::vector<uint32_t>& changes;
    uint16_t tick;
    unsigned Phase(std::size_t n) const { return e[w.elementIdx[n]].state & 3u; }
    bool Interior(std::size_t n) const {
        return n < w.elementIdx.size() && n >= w.simWidth && n + w.simWidth < w.elementIdx.size() &&
            n % w.simWidth != 0 && n % w.simWidth + 1 != w.simWidth;
    }
    void Notify(std::size_t n) {
        ResetStableCellTimerV744825(timers[n]);
        // Reconstructed domain excludes mutable border destinations.
        if (Interior(n)) changes.push_back(static_cast<uint32_t>(n));
    }
    void Clear(std::size_t n) {
        w.elementIdx[n] = vacuum; w.mass[n] = w.temperature[n] = 0.0f;
        w.diseaseIdx[n] = 255; w.diseaseCount[n] = 0;
    }
    void Swap(std::size_t a, std::size_t b) {
        std::swap(w.elementIdx[a], w.elementIdx[b]); std::swap(w.mass[a], w.mass[b]);
        std::swap(w.temperature[a], w.temperature[b]);
        std::swap(w.diseaseIdx[a], w.diseaseIdx[b]); std::swap(w.diseaseCount[a], w.diseaseCount[b]);
        // Infestation tick and disease accumulated-error also swap natively;
        // this module requires the enclosing no-disease domain where both 0.
    }
    void Add(std::size_t n, uint16_t id, float mass, float temperature) {
        const float previous = w.mass[n], total = previous + mass;
        const float oldT = w.temperature[n];
        float mixed = total > 0.0f ? (oldT * previous + temperature * mass) / total : 0.0f;
        mixed = std::max(std::min(mixed, std::max(oldT, temperature)), std::min(oldT, temperature));
        w.mass[n] = total; w.temperature[n] = mixed; w.elementIdx[n] = id;
    }
    bool Gas(std::size_t n) {
        if (!(w.mass[n] > 0.0f) || Phase(n) != 1) return false;
        const uint16_t id = w.elementIdx[n];
        const std::array<std::size_t, 4> cardinal{n + w.simWidth, n - 1, n + 1, n - w.simWidth};
        std::size_t destination = w.mass.size();
        for (unsigned i = 0; i < 4; ++i) {
            const auto q = cardinal[(tick + i) & 3u];
            if ((w.elementIdx[q] == id || w.elementIdx[q] == vacuum) && !(w.properties[q] & 1u)) {
                destination = q; break;
            }
        }
        if (destination == w.mass.size()) {
            const std::array<std::size_t, 2> diagonal{n + w.simWidth - 1, n + w.simWidth + 1};
            const std::array<std::size_t, 2> side{n - 1, n + 1};
            for (unsigned i = 0; i < 2; ++i) {
                const unsigned k = (tick + i) & 1u; const auto q = diagonal[k];
                if (w.elementIdx[q] == id && Phase(side[k]) != 3 && !(w.properties[q] & 1u)) {
                    destination = q; break;
                }
            }
        }
        if (destination == w.mass.size()) return false;
        Add(destination, id, w.mass[n], w.temperature[n]); Clear(n);
        Notify(n); Notify(destination); return true;
    }
    bool LiquidSimple(std::size_t n, const std::array<std::size_t, 4>& candidates) {
        if (w.mass[n] < 0.01f) return false;
        const uint16_t id = w.elementIdx[n]; std::array<std::size_t, 4> targets{}; unsigned count = 0;
        for (const auto q : candidates)
            if ((w.elementIdx[q] == id || w.elementIdx[q] == vacuum) && !(w.properties[q] & 2u)) targets[count++] = q;
        if (count == 0) return false;
        const float amount = w.mass[n] / static_cast<float>(count);
        const float temperature = w.temperature[n];
        for (unsigned i = 0; i < count; ++i) { Add(targets[i], id, amount, temperature); Notify(targets[i]); }
        Clear(n); Notify(n); return true;
    }
    bool Liquid(std::size_t n) {
        if (!(w.mass[n] > 0.0f) || Phase(n) != 2) return false;
        const std::array<std::size_t, 4> candidates{n + 1, n - 1, n + w.simWidth, n - w.simWidth};
        if (LiquidSimple(n, candidates)) return true;
        for (unsigned i = 0; i < 4; ++i) {
            const auto q = candidates[(tick + i) & 3u];
            if (Phase(q) == 1 && Gas(q)) return LiquidSimple(n, candidates);
        }
        return false;
    }
    void Fall(std::size_t cell) {
        // Native headless fall deliberately ignores properties after its first
        // eligibility check. It cascades to support in one call, not one row.
        while (cell >= w.simWidth) {
            const std::size_t below = cell - w.simWidth;
            if (Phase(below) == 3) { Notify(cell); break; }
            Swap(cell, below); Notify(cell); cell = below;
        }
    }
};
} // namespace

bool DecodeSolidElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<SolidElementV744825>& elements, std::string& error) {
    error.clear();
    if (bytes.size() < 4 || U32(bytes.data()) == 0 || U32(bytes.data()) >= 65535 ||
        U32(bytes.data()) > (bytes.size() - 4) / 164) { error = "Invalid solid element table."; return false; }
    std::vector<SolidElementV744825> parsed(U32(bytes.data()));
    for (std::size_t i = 0; i < parsed.size(); ++i) {
        const auto* p = bytes.data() + 4 + i * 164;
        if (U16(p + 8) != i || !std::isfinite(F32(p + 44)) || F32(p + 44) < 0.0f) {
            error = "Invalid solid element index or diagonal minimum mass."; return false;
        }
        parsed[i] = {U32(p), p[10], F32(p + 44), U16(p + 84)};
    }
    elements = std::move(parsed); return true;
}

bool ProcessUnstableSolidCellV744825(LoadedWorldV15& world,
    const std::vector<SolidElementV744825>& elements, uint16_t vacuum,
    std::size_t cell, bool diagonals, GasRandomStateV744825& random,
    std::vector<uint8_t>& timers, std::vector<uint32_t>& changes,
    std::string& error, uint16_t tickCount) {
    error.clear(); const std::size_t count = std::size_t(world.simWidth) * world.simHeight;
    if (world.simWidth < 3 || world.simHeight < 3 || vacuum >= elements.size() ||
        elements[vacuum].hash != 0x2D39BF75 || cell >= count || world.elementIdx.size() != count ||
        world.mass.size() != count || world.temperature.size() != count || world.properties.size() != count ||
        world.diseaseIdx.size() != count || world.diseaseCount.size() != count || timers.size() != count ||
        world.elementIdx[cell] >= elements.size()) { error = "Invalid unstable-solid grid or element table."; return false; }
    const auto& solid = elements[world.elementIdx[cell]];
    if ((solid.state & 11u) != 11u) return true;
    if (cell < world.simWidth || cell + world.simWidth >= count || cell % world.simWidth == 0 ||
        cell % world.simWidth + 1 == world.simWidth || solid.sublimateIndex != 65535) {
        error = "Unstable-solid boundary or sublimation is outside the verified domain."; return false;
    }
    // Finite bounds ensure every displacement product and sum remains finite.
    // Reject disease until the auxiliary history arrays join the owned world.
    for (std::size_t n = 0; n < count; ++n) {
        if (world.elementIdx[n] >= elements.size() || !std::isfinite(world.mass[n]) || world.mass[n] < 0.0f ||
            world.mass[n] > 1.0e10f || !std::isfinite(world.temperature[n]) || world.temperature[n] < 0.0f ||
            world.temperature[n] > 10000.0f || world.diseaseIdx[n] != 255 || world.diseaseCount[n] != 0) {
            error = "Unstable-solid cells require bounded finite values without disease."; return false;
        }
        const std::size_t x = n % world.simWidth, y = n / world.simWidth;
        const bool closedBorder = y == 0 || ((x == 0 || x + 1 == world.simWidth) && y + 1 != world.simHeight);
        if ((closedBorder && (elements[world.elementIdx[n]].state & 3u) != 3u) ||
            (!closedBorder && y + 1 == world.simHeight && world.elementIdx[n] != vacuum)) {
            error = "Unstable-solid displacement requires the original closed sides/bottom and vacuum top.";
            return false;
        }
    }
    auto next = world; auto nextTimers = timers; auto nextRandom = random; std::vector<uint32_t> notifications;
    Context c{next, elements, vacuum, nextTimers, notifications, tickCount};
    auto Timer = [&]() {
        const bool draws = (nextTimers[cell] & 31u) == 31u;
        const uint8_t result = AdvanceStableCellTimerV744825(nextTimers[cell], nextRandom.seed);
        if (draws) ++nextRandom.calls;
        return result;
    };
    const std::size_t below = cell - world.simWidth;
    if (!diagonals) {
        if (c.Phase(below) < 3 && elements[next.elementIdx[below]].hash != 0xA9360B34 && !(next.properties[below] & 4u)) {
            if (Timer() == 0) c.Fall(cell);
        } else ResetStableCellTimerV744825(nextTimers[cell]);
    } else {
        bool checked = false, eligible = false; uint8_t remaining = 255;
        const std::array<std::size_t, 3> destinations{below, below - 1, below + 1};
        const std::array<std::size_t, 3> sides{cell, cell - 1, cell + 1};
        for (unsigned i = 0; i < 3; ++i) {
            const float fraction = i == 0 ? 1.0f : 0.5f;
            const float threshold = i == 0 ? 0.0f : solid.diagonalMinimumMass;
            if (!((next.mass[cell] - threshold) * fraction > 0.0f)) continue;
            const auto n = destinations[i];
            if (c.Phase(n) >= 3 || elements[next.elementIdx[n]].hash == 0xA9360B34 || (next.properties[n] & 4u) ||
                (i != 0 && (c.Phase(sides[i]) == 3 || (next.properties[sides[i]] & 4u)))) continue;
            eligible = true;
            if (!checked) { remaining = Timer(); checked = true; }
            if (remaining != 0) continue;
            ResetStableCellTimerV744825(nextTimers[cell]);
            if (c.Phase(n) == 1) c.Gas(n); else if (c.Phase(n) == 2) c.Liquid(n);
            const float amount = next.mass[cell] * fraction;
            next.elementIdx[n] = next.elementIdx[cell]; next.mass[n] = amount; next.temperature[n] = next.temperature[cell];
            next.diseaseIdx[n] = next.diseaseIdx[cell]; next.diseaseCount[n] = 0;
            c.Notify(n); next.mass[cell] += -amount; c.Notify(cell);
            if (!(next.mass[cell] > std::numeric_limits<float>::min())) { c.Clear(cell); c.Notify(cell); }
            c.Fall(n);
        }
        if (!eligible) ResetStableCellTimerV744825(nextTimers[cell]);
    }
    world = std::move(next); timers = std::move(nextTimers); random = nextRandom;
    changes.insert(changes.end(), notifications.begin(), notifications.end()); return true;
}
