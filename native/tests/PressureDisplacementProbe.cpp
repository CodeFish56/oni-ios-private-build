// Candidate-only driver for retained original full PostProcessCell cases.
// Reproduces density/pressure prefixes before the overpressure branch; fixture
// temperatures and elements exclude later partial heat and offgas actions.
// This file is not part of the simulator library or iOS app target.
#include <array>
#include "PressureBreak.h"
#include "PressureDisplacement.h"
#include "StabilityTimers.h"

#include <cstring>
#include <algorithm>
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
    if (argc != 10) {
        std::cerr << "Usage: PressureDisplacementProbe elements.bin fixture-dir output-dir"
            " simWidth simHeight simCell seed direction tick\n";
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
        const auto table=ReadBytes(argv[1]);
        const auto U16=[&](std::size_t p) { uint16_t v; std::memcpy(&v,table.data()+p,2); return v; };
        const auto U32=[&](std::size_t p) { uint32_t v; std::memcpy(&v,table.data()+p,4); return v; };
        const auto F32=[&](std::size_t p) { float v; std::memcpy(&v,table.data()+p,4); return v; };
        if(table.size()<4 || U32(0)==0 || table.size()<4+164*std::size_t(U32(0))) throw std::runtime_error("Invalid fixture element table");
        std::vector<LiquidElementV744825> elements(U32(0));
        uint16_t water=65535,vacuum=65535;
        for(std::size_t i=0;i<elements.size();++i) {
            const std::size_t p=4+i*164;
            if(U16(p+8)!=i) throw std::runtime_error("Fixture element index mismatch");
            elements[i]={U32(p),table[p+10],F32(p+40),F32(p+44),F32(p+48),F32(p+52)};
            if(elements[i].hash==1836671383u)water=static_cast<uint16_t>(i);
            if(elements[i].hash==0x2D39BF75u)vacuum=static_cast<uint16_t>(i);
        }
        std::vector<PressureBreakElementV744825> pressure;
        std::string error;
        if(!DecodePressureBreakElementsV744825(table,pressure,error))throw std::runtime_error(error);
        const uint32_t cell=U32Argument(argv[6]);
        if(cell>=count || world.elementIdx[cell]!=water || world.mass[cell]<0.01f || U16(4+water*164+84)!=65535)
            throw std::runtime_error("Fixture must be a non-sublimating water postprocess cell");
        uint32_t seed=U32Argument(argv[7]);
        const int direction=std::stoi(argv[8]);
        const uint16_t tick=static_cast<uint16_t>(U32Argument(argv[9]));
        std::vector<uint32_t> changes;
        std::vector<WorldDamageInfoV744825> damage;
        bool density=false,broke=false,displaced=false;
        const auto below=cell-world.simWidth;
        if(elements[world.elementIdx[cell]].state==elements[world.elementIdx[below]].state) {
            seed=seed*214013u+2531011u;
            const float sample=float((seed>>16)&32767u)*3.0518509447574615e-05f;
            if(sample>0.3f && world.elementIdx[below]==water && world.temperature[below]>world.temperature[cell]) {
                const float a=world.temperature[cell],b=world.temperature[below];
                const float total=world.mass[cell]+world.mass[below];
                float temperature=(world.mass[cell]*a+world.mass[below]*b)/total;
                temperature=std::max(std::min(temperature,b),a);
                world.temperature[cell]=world.temperature[below]=temperature;density=true;
            }
        }
        if(!density) {
            if(!CheckLiquidPressureBreakV744825(world,pressure,cell,damage,broke,error))throw std::runtime_error(error);
            if(!broke && !DisplaceOverpressuredWaterV744825(world,elements,water,vacuum,cell,changes,timers,displaced,error,tick))
                throw std::runtime_error(error);
        }
        // This fixture domain has no subsequent partial-heat/offgas action.
        // Invalid input contract checks are candidate-only, not DLL claims.
        for(int bad=0;bad<4;++bad) {
            auto invalid=world;auto invalidTimers=timers;
            std::vector<uint32_t> pending{123};bool result=true;
            uint32_t q=cell;uint16_t w=water;
            if(bad==0)q=static_cast<uint32_t>(count);
            if(bad==1)invalid.mass[cell]=std::numeric_limits<float>::quiet_NaN();
            if(bad==2)invalidTimers.clear();
            if(bad==3)w=vacuum;
            if(DisplaceOverpressuredWaterV744825(invalid,elements,w,vacuum,q,pending,invalidTimers,result,error,tick) ||
                !result || pending.size()!=1 || pending[0]!=123)
                throw std::runtime_error("Invalid pressure displacement was not rejected atomically");
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
        WriteValues(output / "after.changes.sim.bin", changes);
        WriteValues(output / "after.events.worldDamageInfo.bin",damage);
        std::ofstream state(output / "state.json", std::ios::trunc);
        state << "{\n  \"seed\": " << seed << ",\n  \"direction\": " << direction
            << ",\n  \"tick\": " << tick << ",\n  \"density\": " << (density?"true":"false")
            << ",\n  \"broke\": " << (broke?"true":"false") << ",\n  \"displaced\": " << (displaced?"true":"false")
            << ",\n  \"changeCount\": " << changes.size() << ",\n  \"damageCount\": " << damage.size() << "\n}\n";
        if (!state) throw std::runtime_error("Failed writing candidate state.json");
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "PressureDisplacementProbe: " << exception.what() << '\n';
        return 1;
    }
}
