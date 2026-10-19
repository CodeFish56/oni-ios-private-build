#include "PartialTransitions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {
constexpr uint32_t Water = 0x6D796197u, Steam = 0xCA627A30u, Ice = 0x341774ABu;
constexpr uint32_t Vacuum = 0x2D39BF75u, Oxygen = 0xA4E0B340u, CO2 = 0x74DC00EFu;
bool Chain(uint32_t hash) { return hash == Water || hash == Steam || hash == Ice; }
bool Shape(const LoadedWorldV15& w, const std::vector<PhaseElementV744825>& es,
    uint32_t c, uint32_t n, const std::vector<uint8_t>* timers, std::string& error) {
    const std::size_t count = std::size_t(w.simWidth) * w.simHeight;
    if (w.simWidth < 3 || w.simHeight < 3 || c >= count || n >= count ||
        w.elementIdx.size() != count || w.mass.size() != count || w.temperature.size() != count ||
        w.properties.size() != count || w.diseaseIdx.size() != count || w.diseaseCount.size() != count ||
        (timers && timers->size() != count) || w.elementIdx[c] >= es.size() || w.elementIdx[n] >= es.size() ||
        !std::isfinite(w.mass[c]) || !std::isfinite(w.mass[n]) || w.mass[c] < 0 || w.mass[n] < 0 ||
        !std::isfinite(w.temperature[c]) || !std::isfinite(w.temperature[n])) {
        error = "Partial transition requires valid finite local cells and timer storage.";
        return false;
    }
    return true;
}
bool ActiveDomain(const LoadedWorldV15& w, const std::vector<PhaseElementV744825>& es,
    std::string& error) {
    const std::size_t count = w.mass.size();
    if (w.radiation.size() != count || w.backwallMass.size() != count) {
        error = "Partial transition requires explicit zero radiation and backwall storage.";
        return false;
    }
    for (std::size_t c = 0; c < count; ++c) {
        if (w.elementIdx[c] >= es.size()) {
            error = "Partial transition element index is out of bounds.";
            return false;
        }
        const auto& e = es[w.elementIdx[c]];
        if ((!Chain(e.hash) && e.hash != Vacuum && e.hash != Oxygen && e.hash != CO2 && (e.state & 3) != 3) ||
            !std::isfinite(w.mass[c]) || w.mass[c] < 0 || !std::isfinite(w.temperature[c]) ||
            w.temperature[c] < 0 || w.temperature[c] > 10000 || (w.properties[c] & ~0x40u) != 0 ||
            w.diseaseIdx[c] != 255 || w.diseaseCount[c] != 0 || w.radiation[c] != 0 || w.backwallMass[c] != 0) {
            error = "Partial transition supports finite Water/Steam/Ice/O2/CO2/vacuum/solid cells, properties 0 or 0x40, no disease/radiation/backwall.";
            return false;
        }
    }
    return true;
}

struct Context {
    LoadedWorldV15& w;
    const std::vector<PhaseElementV744825>& es;
    uint16_t vacuum, tick;
    std::vector<uint32_t>& changes;
    std::vector<uint8_t>* timers;
    std::string& error;
    bool unsupported = false;
    unsigned Phase(std::size_t c) const { return es[w.elementIdx[c]].state & 3; }
    bool Interior(std::size_t c) const {
        const std::size_t x = c % w.simWidth, y = c / w.simWidth;
        return x > 0 && x + 1 < w.simWidth && y > 0 && y + 1 < w.simHeight;
    }
    bool Access(std::size_t c) {
        if (c < w.mass.size()) return true;
        unsupported = true;
        error = "Partial displacement would access outside the owned simulation grid.";
        return false;
    }
    bool Target(std::size_t c) {
        if (Interior(c)) return true;
        unsupported = true;
        error = "Partial displacement into a simulation border remains unsupported.";
        return false;
    }
    void Notify(std::size_t c) {
        changes.push_back(static_cast<uint32_t>(c));
        if (timers) (*timers)[c] |= 31;
    }
    void Clear(std::size_t c) {
        w.elementIdx[c] = vacuum;
        w.mass[c] = w.temperature[c] = 0;
        w.diseaseIdx[c] = 255;
        w.diseaseCount[c] = 0;
    }
    bool AddMass(std::size_t c, float mass, float temperature) {
        const float oldMass = w.mass[c], oldT = w.temperature[c];
        const float total = oldMass + mass;
        float t = total > 0 ? (oldT * oldMass + temperature * mass) / total : 0;
        t = std::max(std::min(t, std::max(oldT, temperature)), std::min(oldT, temperature));
        if (!std::isfinite(total) || !std::isfinite(t)) {
            unsupported = true;
            error = "Partial displacement mass or temperature overflow.";
            return false;
        }
        w.mass[c] = total;
        w.temperature[c] = t;
        return true;
    }
    bool DisplaceGas(std::size_t c) {
        if (!(w.mass[c] > 0) || Phase(c) != 1) return false;
        if (!Target(c)) return false;
        const uint16_t gas = w.elementIdx[c];
        const std::array<std::size_t, 4> target{c + w.simWidth, c - 1, c + 1, c - w.simWidth};
        std::size_t destination = w.mass.size();
        for (unsigned i = 0; i < 4; ++i) {
            const std::size_t d = target[(tick + i) & 3];
            if (!Access(d)) return false;
            if ((w.elementIdx[d] == gas || w.elementIdx[d] == vacuum) && (w.properties[d] & 1) == 0) {
                destination = d;
                break;
            }
        }
        if (destination == w.mass.size()) {
            const std::array<std::size_t, 2> diagonal{c + w.simWidth - 1, c + w.simWidth + 1};
            const std::array<std::size_t, 2> side{c - 1, c + 1};
            for (unsigned i = 0; i < 2; ++i) {
                const unsigned at = (tick + i) & 1;
                if (!Access(diagonal[at]) || !Access(side[at])) return false;
                if (w.elementIdx[diagonal[at]] == gas && Phase(side[at]) != 3 &&
                    (w.properties[diagonal[at]] & 1) == 0) {
                    destination = diagonal[at];
                    break;
                }
            }
        }
        if (destination == w.mass.size()) return false;
        if (!Target(destination) || !AddMass(destination, w.mass[c], w.temperature[c])) return false;
        w.elementIdx[destination] = gas;
        Clear(c);
        Notify(c);
        Notify(destination);
        return true;
    }
    bool LiquidSimple(std::size_t c, uint16_t liquid, const std::array<std::size_t, 4>& adjacent) {
        if (w.mass[c] < .01f) return false;
        std::array<std::size_t, 4> destinations{};
        std::size_t count = 0;
        for (const std::size_t d : adjacent) {
            if (!Access(d)) return false;
            if ((w.elementIdx[d] == liquid || w.elementIdx[d] == vacuum) && (w.properties[d] & 2) == 0) {
                if (!Target(d)) return false;
                destinations[count++] = d;
            }
        }
        if (count == 0) return false;
        const float part = w.mass[c] / static_cast<float>(count);
        const float temperature = w.temperature[c];
        for (std::size_t i = 0; i < count; ++i) {
            if (!AddMass(destinations[i], part, temperature)) return false;
            w.elementIdx[destinations[i]] = liquid;
            Notify(destinations[i]);
        }
        Clear(c);
        Notify(c);
        return true;
    }
    bool DisplaceLiquid(std::size_t c, uint16_t liquid) {
        if (!(w.mass[c] > 0) || Phase(c) != 2) return false;
        if (!Target(c)) return false;
        const std::array<std::size_t, 4> adjacent{c + 1, c - 1, c + w.simWidth, c - w.simWidth};
        if (LiquidSimple(c, liquid, adjacent)) return true;
        if (unsupported) return false;
        for (unsigned i = 0; i < 4; ++i) {
            const std::size_t d = adjacent[(tick + i) & 3];
            if (!Access(d)) return false;
            if (Phase(d) == 1 && DisplaceGas(d)) return LiquidSimple(c, liquid, adjacent);
            if (unsupported) return false;
        }
        return false;
    }
};

template<class Operation>
bool Staged(LoadedWorldV15& w, const std::vector<PhaseElementV744825>& es, uint16_t tick,
    std::vector<uint32_t>& changes, bool& nativeResult, std::string& error,
    std::vector<uint8_t>* timers, Operation operation) {
    if (!ActiveDomain(w, es, error)) return false;
    uint16_t vacuum = 65535;
    for (std::size_t i = 0; i < es.size(); ++i) if (es[i].hash == Vacuum) vacuum = static_cast<uint16_t>(i);
    if (vacuum == 65535) { error = "Partial transitions require the captured vacuum element."; return false; }
    LoadedWorldV15 next = w;
    auto nextChanges = changes;
    std::vector<uint8_t> nextTimers;
    if (timers) nextTimers = *timers;
    Context ctx{next, es, vacuum, tick, nextChanges, timers ? &nextTimers : nullptr, error};
    const bool result = operation(ctx);
    if (ctx.unsupported) return false;
    w = std::move(next);
    changes = std::move(nextChanges);
    if (timers) *timers = std::move(nextTimers);
    nativeResult = result;
    return true;
}
}

bool ApplyPartialHeatTransitionV744825(LoadedWorldV15& w,
    const std::vector<PhaseElementV744825>& es, uint32_t cell, uint32_t neighbour,
    uint16_t tick, GasRandomStateV744825& random, std::vector<uint32_t>& changes,
    bool& nativeResult, std::string& error, std::vector<uint8_t>* timers) {
    (void)random; // Neither native helper consumes the shared LCG.
    if (!Shape(w, es, cell, neighbour, timers, error)) return false;
    const auto& e = es[w.elementIdx[cell]];
    const auto& ne = es[w.elementIdx[neighbour]];
    const float temperature = w.temperature[cell], hot = w.temperature[neighbour];
    const float targetT = e.highTemperature + 3.0f;
    if (w.mass[cell] < 5.0f || e.highTransition == 65535 || hot <= targetT ||
        hot < ne.lowTemperature + 13.0f || temperature >= e.highTemperature - 3.0f) {
        nativeResult = false;
        return true;
    }
    if (e.highTransition >= es.size() || !Chain(e.hash) || !Chain(es[e.highTransition].hash)) {
        error = "Partial heat transition currently requires the Water/Steam/Ice chain.";
        return false;
    }
    const float required = ((targetT - temperature) * e.specificHeatCapacity) * 5.0f;
    const float capacity = w.mass[neighbour] * ne.specificHeatCapacity;
    if (capacity * 10.0f < required) { nativeResult = false; return true; }
    if (!(capacity > 0) || !std::isfinite(capacity) || !std::isfinite(required)) {
        error = "Partial heat transition requires a finite positive hot-cell heat capacity.";
        return false;
    }
    const float cooled = (hot * capacity - required) / capacity;
    const unsigned targetPhase = es[e.highTransition].state & 3;
    if (targetPhase != 1 && targetPhase != 2) {
        error = "Partial heat transition target phase remains unsupported.";
        return false;
    }
    return Staged(w, es, tick, changes, nativeResult, error, timers, [&](Context& ctx) {
        if (!ctx.Interior(cell) || !ctx.Interior(neighbour)) {
            ctx.unsupported = true; error = "Nontrivial partial transition requires game-grid cells."; return false;
        }
        auto& out = ctx.w;
        out.mass[cell] -= 5.0f;
        out.temperature[neighbour] = cooled;
        if (targetPhase == 1) {
            // The original indexes above the transforming cold cell, not
            // above the hot neighbour supplying heat (4BD46 -> 4BD58).
            const std::size_t above = std::size_t(cell) + out.simWidth;
            if (!ctx.Access(above)) return false;
            if (ctx.Phase(above) == 1 && ctx.DisplaceGas(above)) {
                out.mass[above] = 5.0f;
                out.temperature[above] = targetT;
                out.elementIdx[above] = e.highTransition;
                ctx.Notify(above);
                return true;
            }
            if (ctx.unsupported) return false;
        }
        const uint16_t oldElement = out.elementIdx[cell];
        if (ctx.DisplaceLiquid(cell, oldElement)) {
            // Headless SpawnFallingLiquid is false: place the 5 kg converted
            // mass in the cell just cleared by liquid displacement.
            out.mass[cell] = 5.0f;
            out.temperature[cell] = targetT;
            out.elementIdx[cell] = e.highTransition;
            ctx.Notify(cell);
            return true;
        }
        if (ctx.unsupported) return false;
        out.mass[cell] += 5.0f;
        out.temperature[neighbour] = hot;
        // A failed liquid displacement can already have moved gas. Preserve
        // its notifications and state even though native return is false.
        return false;
    });
}

bool ApplyPartialMeltV744825(LoadedWorldV15& w,
    const std::vector<PhaseElementV744825>& es, uint32_t cell, uint32_t neighbour,
    uint16_t tick, GasRandomStateV744825& random, std::vector<uint32_t>& changes,
    bool& nativeResult, std::string& error, std::vector<uint8_t>* timers) {
    (void)random;
    if (!Shape(w, es, cell, neighbour, timers, error)) return false;
    const auto& gas = es[w.elementIdx[cell]];
    const auto& solid = es[w.elementIdx[neighbour]];
    const float hot = w.temperature[cell], cold = w.temperature[neighbour];
    const float targetT = solid.highTemperature + 3.0f;
    if ((gas.state & 3) > 1 || (solid.state & 3) != 3 || w.mass[neighbour] <= 5.0f ||
        (w.properties[neighbour] & 0x48) != 0 || hot <= targetT ||
        cold >= solid.highTemperature - 3.0f || solid.highTransition == 65535) {
        nativeResult = false;
        return true;
    }
    if (solid.highTransition >= es.size()) { error = "Partial melt target index is invalid."; return false; }
    const auto& target = es[solid.highTransition];
    const float required = ((targetT - cold) * target.specificHeatCapacity) * 5.0f;
    const float capacity = w.mass[cell] * gas.specificHeatCapacity;
    const float available = (hot - (gas.lowTemperature + 6.0f)) * capacity;
    if (available < required || (target.state & 3) != 2) { nativeResult = false; return true; }
    if (solid.hash != Ice || target.hash != Water || !(capacity > 0) ||
        !std::isfinite(capacity) || !std::isfinite(required) || !std::isfinite(available)) {
        error = "Partial melt currently supports a finite Ice-to-Water transition beside supported gas.";
        return false;
    }
    const float cooled = (capacity * hot - required) / capacity;
    return Staged(w, es, tick, changes, nativeResult, error, timers, [&](Context& ctx) {
        if (!ctx.Interior(cell) || !ctx.Interior(neighbour)) {
            ctx.unsupported = true; error = "Nontrivial partial melt requires game-grid cells."; return false;
        }
        // Original SpawnFallingLiquid returns false immediately in headless.
        if (!ctx.DisplaceGas(cell)) return false;
        auto& out = ctx.w;
        out.temperature[cell] = cooled;
        out.mass[cell] = 5.0f;
        // This overwrites cooled on the original fallback path. The displaced
        // gas keeps its original hot temperature; do not "correct" this quirk.
        out.temperature[cell] = targetT;
        out.elementIdx[cell] = solid.highTransition;
        out.mass[neighbour] -= 5.0f;
        ctx.Notify(cell);
        return true;
    });
}
