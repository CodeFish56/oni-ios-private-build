// Candidate-only test driver for retained original direct PostProcessCell cases.
// This file is not part of the simulator library or iOS app target.
#include <array>
#include "SolidBehavior.h"

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
        std::cerr << "Usage: SolidProbe elements.bin fixture-dir output-dir"
            " simWidth simHeight cell diagonals steps seed displacementDirection tick\n";
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
        // These vectors are not observed by the restricted gas postprocessor.
        // Populate them for a valid world object; they are NOT coverage claims.
        world.backwallElement.assign(count, 0);
        world.backwallMass.assign(count, 0.0f);
        world.backwallTemperature.assign(count, 0.0f);
        auto flow = ReadValues<std::array<float, 4>>(input / "before.flow.bin", count);
        auto timers = ReadValues<uint8_t>(input / "before.timers.bin", count);
        std::vector<SolidElementV744825> elements;
        uint16_t vacuum = 65535;
        std::string error;
        if (!DecodeSolidElementsV744825(ReadBytes(argv[1]), elements, error))
            throw std::runtime_error(error);
        for (std::size_t i = 0; i < elements.size(); ++i)
            if (elements[i].hash == 0x2D39BF75) vacuum = static_cast<uint16_t>(i);
        GasRandomStateV744825 random;
        random.seed = U32Argument(argv[9]);
        random.displacementDirection = std::stoi(argv[10]);
        std::vector<uint32_t> changes;
        std::filesystem::create_directories(output);
        auto Snapshot = [&](const std::string& phase) {
        auto& result = world;

#define WRITE_GRID(member) WriteValues(output / (phase + ".updated." #member ".bin"), result.member)
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
        WriteValues(output / (phase + ".flow.bin"), flow);
        WriteValues(output / (phase + ".timers.bin"), timers);
        WriteValues(output / (phase + ".changes.sim.bin"), changes);
        std::ofstream state(output / (phase == "after" ? "state.json" : phase + ".state.json"), std::ios::trunc);
        state << "{\n  \"seed\": " << random.seed
            << ",\n  \"displacementDirection\": " << random.displacementDirection
            << ",\n  \"rngCalls\": " << random.calls
            << ",\n  \"changeCount\": " << changes.size() << "\n}\n";
        if (!state) throw std::runtime_error("Failed writing candidate state.json");
        };
        const uint32_t steps = U32Argument(argv[8]);
        if (steps == 0 || steps > 1000) throw std::runtime_error("Invalid solid step count");
        for (uint32_t step = 0; step < steps; ++step) {
            if (!ProcessUnstableSolidCellV744825(world, elements, vacuum,
                U32Argument(argv[6]), U32Argument(argv[7]) != 0, random,
                timers, changes, error, static_cast<uint16_t>(U32Argument(argv[11])))) {
                throw std::runtime_error(error);
            }
            if (steps > 1) Snapshot("step-" + std::to_string(step));
        }
        Snapshot("after");
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "SolidProbe: " << exception.what() << '\n';
        return 1;
    }
}
