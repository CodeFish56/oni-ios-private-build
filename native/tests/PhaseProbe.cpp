// Candidate-only test driver for retained original direct full-state transition and partial-heat no-op cases.
// This file is not part of the simulator library or iOS app target.
#include <array>
#include "PhaseTransitions.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

template <class T>
std::vector<T> ReadValues(const std::filesystem::path& path, std::size_t count) {
    const auto bytes = ReadBytes(path);
    if (bytes.size() != count * sizeof(T))
        throw std::runtime_error("Unexpected byte length: " + path.string());
    std::vector<T> values(count);
    if (!bytes.empty()) std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
}

template <class T>
void WriteValues(const std::filesystem::path& path, const std::vector<T>& values) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot write " + path.string());
    if (!values.empty()) output.write(reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(T)));
    if (!output) throw std::runtime_error("Failed writing " + path.string());
}

uint32_t U32Argument(const char* text) {
    const auto value = std::stoull(text);
    if (value > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Argument exceeds uint32");
    return static_cast<uint32_t>(value);
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 8) {
        std::cerr << "Usage: PhaseProbe elements.bin fixture-dir output-dir simWidth simHeight simCell partialMode\n";
        return 2;
    }
    try {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        static_assert(sizeof(std::array<float, 4>) == 16);
        const uint16_t endianProbe = 1;
        if (*reinterpret_cast<const uint8_t*>(&endianProbe) != 1)
            throw std::runtime_error("Retained raw fixtures require a little-endian host");
        const std::filesystem::path input(argv[2]), output(argv[3]);
        const uint32_t width = U32Argument(argv[4]), height = U32Argument(argv[5]);
        if (width < 3 || height < 3 || uint64_t(width) * height > 1024 * 1024)
            throw std::runtime_error("Invalid fixture grid dimensions");
        const std::size_t count = std::size_t(width) * height;
        auto readWorld = [&](const std::string& view) {
            LoadedWorldV15 world;
            world.simWidth = width; world.simHeight = height;
#define READ_GRID(member, type) world.member = ReadValues<type>(input / ("before." + view + "." #member ".bin"), count)
            READ_GRID(elementIdx, uint16_t); READ_GRID(temperature, float); READ_GRID(mass, float);
            READ_GRID(properties, uint8_t); READ_GRID(insulation, uint8_t); READ_GRID(strengthInfo, uint8_t);
            READ_GRID(radiation, float); READ_GRID(diseaseIdx, uint8_t); READ_GRID(diseaseCount, int32_t);
#undef READ_GRID
            // Backwalls are not read by the restricted flow module. These are
            // valid storage defaults, not a backwall coverage claim.
            world.backwallElement.assign(count, 0); world.backwallMass.assign(count, 0.f);
            world.backwallTemperature.assign(count, 0.f);
            return world;
        };
        LoadedWorldV15 source = readWorld("source"), result = readWorld("updated");
        auto flow = ReadValues<std::array<float, 4>>(input / "before.flow.bin", count);
        auto timers = ReadValues<uint8_t>(input / "before.timers.bin", count);
        std::vector<PhaseElementV744825> elements;
        std::string error;
        if (!DecodePhaseElementsV744825(ReadBytes(argv[1]), elements, error))
            throw std::runtime_error(error);
        std::vector<uint32_t> changes;
        std::vector<PhaseOreSpawnV744825> oreEvents;
        std::vector<int32_t> cellMeltedEvents;
        std::vector<uint8_t> visibility(count,0);
        if (std::filesystem::exists(input / "before.visibilityGame.bin")) {
            auto gameVisibility = ReadValues<uint8_t>(input / "before.visibilityGame.bin",(width-2)*(height-2));
            for (uint32_t n=0;n<count;++n) {
                const int64_t game=(int64_t(n/width)-1)*(width-2)+int64_t(n%width)-1;
                if (game>=0 && game<int64_t(gameVisibility.size())) visibility[n]=gameVisibility[std::size_t(game)];
            }
        }
        const bool isPartial=U32Argument(argv[7])!=0;
        const uint32_t cell=U32Argument(argv[6]);
        const LoadedWorldV15 initial = result;
        const auto initialTimers = timers;
        bool transitioned=false, noop=false;
        if (isPartial) noop=IsPartialHeatTransitionNoopV744825(result,elements,cell,cell+1);
        else if (!ApplyStateTransitionV744825(result,elements,cell,changes,transitioned,error,&timers,&visibility,&oreEvents,&cellMeltedEvents))
            throw std::runtime_error(error);
        unsigned missingOutputRejections = 0;
        auto sameWorld = [](const LoadedWorldV15& a, const LoadedWorldV15& b) {
            return a.elementIdx==b.elementIdx && a.temperature==b.temperature && a.mass==b.mass &&
                a.properties==b.properties && a.insulation==b.insulation && a.strengthInfo==b.strengthInfo &&
                a.radiation==b.radiation && a.diseaseIdx==b.diseaseIdx && a.diseaseCount==b.diseaseCount &&
                a.backwallElement==b.backwallElement && a.backwallMass==b.backwallMass &&
                a.backwallTemperature==b.backwallTemperature;
        };
        for (unsigned missing=0; missing<2; ++missing) {
            if (isPartial || (missing==0 ? oreEvents.empty() : cellMeltedEvents.empty())) continue;
            auto rejectedWorld=initial;
            auto rejectedTimers=initialTimers;
            std::vector<uint32_t> rejectedChanges{cell};
            std::vector<PhaseOreSpawnV744825> rejectedOre(1);
            std::vector<int32_t> rejectedMelted{12345};
            const auto sentinelOre=rejectedOre;
            bool rejectedTransition=true;
            std::string rejectedError;
            if (ApplyStateTransitionV744825(rejectedWorld,elements,cell,rejectedChanges,rejectedTransition,
                    rejectedError,&rejectedTimers,&visibility,missing==0?nullptr:&rejectedOre,
                    missing==1?nullptr:&rejectedMelted) || rejectedError.empty() ||
                !rejectedTransition || !sameWorld(rejectedWorld,initial) || rejectedTimers!=initialTimers ||
                rejectedChanges!=std::vector<uint32_t>{cell} || rejectedMelted!=std::vector<int32_t>{12345} ||
                rejectedOre.size()!=1 || std::memcmp(rejectedOre.data(),sentinelOre.data(),sizeof(PhaseOreSpawnV744825))!=0)
                throw std::runtime_error("Missing phase output storage did not reject before mutation");
            ++missingOutputRejections;
        }
        std::filesystem::create_directories(output);
        auto writeWorld = [&](const std::string& view, const LoadedWorldV15& world) {
#define WRITE_GRID(member) WriteValues(output / ("after." + view + "." #member ".bin"), world.member)
            WRITE_GRID(elementIdx); WRITE_GRID(temperature); WRITE_GRID(mass); WRITE_GRID(properties);
            WRITE_GRID(insulation); WRITE_GRID(strengthInfo); WRITE_GRID(radiation);
            WRITE_GRID(diseaseIdx); WRITE_GRID(diseaseCount);
#undef WRITE_GRID
        };
        writeWorld("source", source); writeWorld("updated", result);
        WriteValues(output / "after.flow.bin", flow);
        WriteValues(output / "after.timers.bin", timers);
        WriteValues(output / "after.changes.sim.bin", changes);
        WriteValues(output / "after.oreSpawns.bin", oreEvents);
        WriteValues(output / "after.cellMelted.bin", cellMeltedEvents);
        std::ofstream state(output / "state.json", std::ios::trunc);
        state << "{\n  \"changeCount\": " << changes.size() << ",\n  \"transitioned\": " << (transitioned?"true":"false") << ",\n  \"partialNoop\": " << (noop?"true":"false") << ",\n  \"missingOutputRejections\": " << missingOutputRejections << "\n}\n";
        if (!state) throw std::runtime_error("Failed writing candidate state.json");
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "PhaseProbe: " << exception.what() << '\n';
        return 1;
    }
}
