// Candidate-only driver for retained original direct DoPressureBreak cases.
// This file is not part of the simulator library or iOS app target.
#include <array>
#include "PressureBreak.h"

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
    if (argc != 9) {
        std::cerr << "Usage: PressureBreakProbe elements.bin fixture-dir output-dir"
            " simWidth simHeight simCell pressure offset\n";
        return 2;
    }
    try {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        static_assert(sizeof(std::array<float, 4>) == 16);
        const uint16_t endianProbe = 1;
        if (*reinterpret_cast<const uint8_t*>(&endianProbe) != 1)
            throw std::runtime_error("Retained raw fixtures require a little-endian host");
        const std::filesystem::path input(argv[2]), output(argv[3]);
        LoadedWorldV15 world;
        world.simWidth = U32Argument(argv[4]);
        world.simHeight = U32Argument(argv[5]);
        if (world.simWidth < 3 || world.simHeight < 3 ||
            uint64_t(world.simWidth) * world.simHeight > 1024 * 1024)
            throw std::runtime_error("Invalid fixture grid dimensions");
        const std::size_t count = std::size_t(world.simWidth) * world.simHeight;
#define READ_GRID(member, type) world.member = ReadValues<type>(input / "before.updated." #member ".bin", count)
        READ_GRID(elementIdx, uint16_t);
        READ_GRID(temperature, float);
        READ_GRID(mass, float);
        READ_GRID(properties, uint8_t);
        READ_GRID(insulation, uint8_t);
        READ_GRID(strengthInfo, uint8_t);
        READ_GRID(radiation, float);
        READ_GRID(diseaseIdx, uint8_t);
        READ_GRID(diseaseCount, int32_t);
#undef READ_GRID
        // Backwall vectors are not observed by the pressure-break helper.
        // Populate them for a valid world object; they are NOT coverage claims.
        world.backwallElement.assign(count, 0);
        world.backwallMass.assign(count, 0.0f);
        world.backwallTemperature.assign(count, 0.0f);
        auto flow = ReadValues<std::array<float, 4>>(input / "before.flow.bin", count);
        auto timers = ReadValues<uint8_t>(input / "before.timers.bin", count);
        std::vector<PressureBreakElementV744825> elements;
        std::string error;
        if (!DecodePressureBreakElementsV744825(ReadBytes(argv[1]), elements, error)) throw std::runtime_error(error);
        std::vector<WorldDamageInfoV744825> changes;
        bool broke=false;
        if (!DoPressureBreakV744825(world, elements, U32Argument(argv[6]), std::stof(argv[7]), std::stoi(argv[8]), changes, broke, error))
            throw std::runtime_error(error);
        // Failure paths must preserve the caller's existing event queue and
        // out boolean. These are component contract checks, not DLL behavior
        // claims for invalid native inputs (the DLL aborts for some of them).
        for (int invalid=0; invalid<4; ++invalid) {
            std::vector<WorldDamageInfoV744825> pending{{123,456}};
            bool sentinel=true;
            const uint32_t badCell=invalid==0?uint32_t(count):U32Argument(argv[6]);
            const float badPressure=invalid==1?std::numeric_limits<float>::quiet_NaN():std::stof(argv[7]);
            const int32_t badOffset=invalid==2?0:std::stoi(argv[8]);
            auto badWorld=world;
            if(invalid==3) badWorld.strengthInfo.clear();
            if(DoPressureBreakV744825(badWorld,elements,badCell,badPressure,badOffset,pending,sentinel,error) ||
                !sentinel || pending.size()!=1 || pending[0].gameCell!=123 || pending[0].damageSourceOffset!=456)
                throw std::runtime_error("Invalid pressure-break input was not rejected atomically");
        }
        // Every fixture has obstacles on at most its selected axis. Where the
        // retained input pressure equals native mass/maxMass exactly, the
        // four-direction caller must return the same result and queue.
        const uint32_t center=U32Argument(argv[6]);
        if(world.mass[center]/elements[world.elementIdx[center]].maxMass==std::stof(argv[7])) {
            std::vector<WorldDamageInfoV744825> combined;
            bool combinedBroke=false;
            if(!CheckLiquidPressureBreakV744825(world,elements,center,combined,combinedBroke,error) ||
                combinedBroke!=broke || combined.size()!=changes.size() ||
                (!combined.empty() && std::memcmp(combined.data(),changes.data(),changes.size()*sizeof(changes[0]))!=0))
                throw std::runtime_error("Liquid pressure-break caller disagrees with the directional fixture");
        }
        const auto& result=world;
        std::filesystem::create_directories(output);
#define WRITE_GRID(member) WriteValues(output / "after.updated." #member ".bin", result.member)
        WRITE_GRID(elementIdx);
        WRITE_GRID(temperature);
        WRITE_GRID(mass);
        WRITE_GRID(properties);
        WRITE_GRID(insulation);
        WRITE_GRID(strengthInfo);
        WRITE_GRID(radiation);
        WRITE_GRID(diseaseIdx);
        WRITE_GRID(diseaseCount);
#undef WRITE_GRID
        WriteValues(output / "after.flow.bin", flow);
        WriteValues(output / "after.timers.bin", timers);
        WriteValues(output / "after.events.worldDamageInfo.bin", changes);
        std::ofstream state(output / "state.json", std::ios::trunc);
        state << "{\n  \"broke\": " << (broke?"true":"false") << ",\n  \"eventCount\": " << changes.size() << "\n}\n";
        if (!state) throw std::runtime_error("Failed writing candidate state.json");
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "PressureBreakProbe: " << exception.what() << '\n';
        return 1;
    }
}
