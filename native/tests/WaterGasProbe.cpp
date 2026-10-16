// Candidate-only test driver for retained original direct UpdateLiquid cases.
// This file is not part of the simulator library or iOS app target.
#include <array>
#include "LiquidFlow.h"

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
        std::cerr << "Usage: WaterGasProbe elements.bin fixture-dir output-dir simWidth simHeight simCell tick\n";
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
        const auto tick = U32Argument(argv[7]);
        if (tick > 65535) throw std::runtime_error("Tick exceeds uint16");
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
        std::vector<LiquidElementV744825> elements;
        uint16_t water = 65535, vacuum = 65535;
        std::string error;
        if (!DecodeLiquidElementsV744825(ReadBytes(argv[1]), elements, water, vacuum, error))
            throw std::runtime_error(error);
        std::vector<uint32_t> changes;
        if (!UpdateWaterCellV744825(source, elements, water, vacuum, U32Argument(argv[6]),
                result, changes, flow, error, &timers, static_cast<uint16_t>(tick)))
            throw std::runtime_error(error);
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
        state << "{\n  \"changeCount\": " << changes.size() << "\n}\n";
        if (!state) throw std::runtime_error("Failed writing candidate state.json");
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "WaterGasProbe: " << exception.what() << '\n';
        return 1;
    }
}
