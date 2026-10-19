// Candidate-only test driver for retained original direct full-state transition and partial-heat no-op cases.
// This file is not part of the simulator library or iOS app target.
#include <array>
#include "PartialTransitions.h"

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
    if (argc != 12) {
        std::cerr << "Usage: PartialProbe elements.bin fixture-dir output-dir simWidth simHeight simCell neighbour melt tick seed direction\n";
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
        // A real nontrivial heat path debits 5 kg and cools the neighbour
        // before displacement reaches this unsupported border. Failure must
        // leave the complete caller-owned state and output parameters intact.
        {
            LoadedWorldV15 rejected=result;
            uint16_t vacuum=65535, water=65535, ice=65535, solid=65535;
            for (std::size_t i=0;i<elements.size();++i) {
                if (elements[i].hash==0x2D39BF75u) vacuum=static_cast<uint16_t>(i);
                if (elements[i].hash==0x6D796197u) water=static_cast<uint16_t>(i);
                if (elements[i].hash==0x341774ABu) ice=static_cast<uint16_t>(i);
                if (elements[i].hash==0x6D95058Cu) solid=static_cast<uint16_t>(i);
            }
            if (vacuum==65535 || water==65535 || ice==65535 || solid==65535)
                throw std::runtime_error("Atomic fixture materials missing");
            rejected.elementIdx.assign(count,solid);
            rejected.mass.assign(count,9999.f); rejected.temperature.assign(count,0.f);
            rejected.properties.assign(count,0);
            rejected.diseaseIdx.assign(count,255); rejected.diseaseCount.assign(count,0);
            const uint32_t c=2*width+1,n=c+1;
            rejected.elementIdx[c]=water; rejected.mass[c]=10; rejected.temperature[c]=300;
            rejected.elementIdx[n]=ice; rejected.mass[n]=100; rejected.temperature[n]=500;
            rejected.elementIdx[c-1]=vacuum; rejected.mass[c-1]=rejected.temperature[c-1]=0;
            const LoadedWorldV15 original=rejected;
            std::vector<uint32_t> events{42};
            std::vector<uint8_t> atomicTimers(count,0xA2);
            GasRandomStateV744825 rng{123,-1,7};
            bool native=true;
            if (ApplyPartialHeatTransitionV744825(rejected,elements,c,n,0,rng,events,native,error,&atomicTimers) ||
                !native || events!=std::vector<uint32_t>{42} || atomicTimers!=std::vector<uint8_t>(count,0xA2) ||
                rng.seed!=123 || rng.displacementDirection!=-1 || rng.calls!=7 ||
                rejected.elementIdx!=original.elementIdx || rejected.mass!=original.mass ||
                rejected.temperature!=original.temperature || rejected.properties!=original.properties ||
                rejected.diseaseIdx!=original.diseaseIdx || rejected.diseaseCount!=original.diseaseCount ||
                rejected.insulation!=original.insulation || rejected.strengthInfo!=original.strengthInfo ||
                rejected.radiation!=original.radiation || rejected.backwallElement!=original.backwallElement ||
                rejected.backwallMass!=original.backwallMass || rejected.backwallTemperature!=original.backwallTemperature)
                throw std::runtime_error("Partial transition unsupported-border rejection was not atomic");
            error.clear();
        }
        std::vector<uint32_t> changes;
        const uint32_t cell=U32Argument(argv[6]), neighbour=U32Argument(argv[7]);
        const bool melt=U32Argument(argv[8])!=0;
        const uint32_t tick=U32Argument(argv[9]);
        if (tick>65535) throw std::runtime_error("Tick exceeds native uint16");
        GasRandomStateV744825 random;
        random.seed=U32Argument(argv[10]);
        random.displacementDirection=std::stoi(argv[11]);
        bool nativeResult=false;
        const bool success=melt
            ? ApplyPartialMeltV744825(result,elements,cell,neighbour,static_cast<uint16_t>(tick),random,changes,nativeResult,error,&timers)
            : ApplyPartialHeatTransitionV744825(result,elements,cell,neighbour,static_cast<uint16_t>(tick),random,changes,nativeResult,error,&timers);
        if (!success) throw std::runtime_error(error);
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
        std::ofstream state(output / "state.json", std::ios::trunc);
        state << "{\n  \"changeCount\": " << changes.size()
            << ",\n  \"nativeResult\": " << (nativeResult?"true":"false")
            << ",\n  \"seed\": " << random.seed
            << ",\n  \"displacementDirection\": " << random.displacementDirection
            << ",\n  \"rngCalls\": " << random.calls << ",\n  \"atomicRejectionChecked\": true\n}\n";
        if (!state) throw std::runtime_error("Failed writing candidate state.json");
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "PartialProbe: " << exception.what() << '\n';
        return 1;
    }
}
