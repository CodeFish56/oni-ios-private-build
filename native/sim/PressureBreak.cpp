#include "PressureBreak.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace {
uint16_t U16(const std::vector<uint8_t>& b, std::size_t p) { return uint16_t(b[p] | (uint16_t(b[p+1]) << 8)); }
uint32_t U32(const std::vector<uint8_t>& b, std::size_t p) { return uint32_t(U16(b,p)) | (uint32_t(U16(b,p+2)) << 16); }
float F32(const std::vector<uint8_t>& b, std::size_t p) { const uint32_t bits=U32(b,p); float f; std::memcpy(&f,&bits,4); return f; }
bool GameCell(const LoadedWorldV15& world, int64_t cell, int32_t& output) {
    const int64_t converted = (cell / world.simWidth - 1) * (world.simWidth - 2) + cell % world.simWidth - 1;
    output = static_cast<int32_t>(converted);
    return converted >= 0 && converted < int64_t(world.GameWidth()) * world.GameHeight();
}
}

bool DecodePressureBreakElementsV744825(const std::vector<uint8_t>& bytes,
    std::vector<PressureBreakElementV744825>& elements, std::string& error) {
    error.clear();
    if (bytes.size()<4 || U32(bytes,0)==0 || U32(bytes,0)>=65535 || U32(bytes,0)>(bytes.size()-4)/164) {
        error="Invalid pressure-break element table."; return false;
    }
    std::vector<PressureBreakElementV744825> parsed(U32(bytes,0));
    for(std::size_t i=0;i<parsed.size();++i) {
        const std::size_t at=4+i*164;
        if(U16(bytes,at+8)!=i) { error="Pressure-break element index mismatch."; return false; }
        auto& e=parsed[i]; e.state=bytes[at+10]; e.strength=F32(bytes,at+64); e.maxMass=F32(bytes,at+52);
        if(!std::isfinite(e.strength)||e.strength<0 || !std::isfinite(e.maxMass)||e.maxMass<0) {
            error="Invalid pressure-break material constants."; return false;
        }
    }
    elements=std::move(parsed); return true;
}

bool DoPressureBreakV744825(const LoadedWorldV15& world,
    const std::vector<PressureBreakElementV744825>& elements,
    uint32_t cell, float pressure, int32_t offset,
    std::vector<WorldDamageInfoV744825>& events, bool& broke, std::string& error) {
    error.clear();
    const std::size_t count=std::size_t(world.simWidth)*world.simHeight;
    if(world.simWidth<3 || world.simHeight<3 || count>std::size_t(std::numeric_limits<int32_t>::max()) ||
        world.elementIdx.size()!=count || world.mass.size()!=count || world.properties.size()!=count ||
        world.strengthInfo.size()!=count || cell>=count || !std::isfinite(pressure)||pressure<0 ||
        (offset!=-int32_t(world.simWidth) && offset!=-1 && offset!=1 && offset!=int32_t(world.simWidth))) {
        error="Invalid pressure-break grid or arguments."; return false;
    }
    float resistance=1.0f;
    int walls=0;
    int64_t current=cell;
    uint16_t id=0;
    auto read=[&]() {
        if(current<0 || current>=int64_t(count) || world.elementIdx[std::size_t(current)]>=elements.size()) {
            error="Pressure-break scan leaves the verified grid."; return false;
        }
        id=world.elementIdx[std::size_t(current)]; return true;
    };
    for(int step=0;step<3;++step) {
        current+=offset;
        if(!read()) return false;
        const auto& e=elements[id];
        if((e.state&3)!=3) break;
        if((e.state&4)!=0 || (world.properties[std::size_t(current)]&8)!=0) { broke=false; return true; }
        if(!std::isfinite(world.mass[std::size_t(current)]) || world.mass[std::size_t(current)]<0 ||
            !std::isfinite(e.maxMass) || e.maxMass<=0 || !std::isfinite(e.strength)||e.strength<0) {
            error="Unsupported pressure-break solid numeric state."; return false;
        }
        ++walls;
        const uint8_t strength=world.strengthInfo[std::size_t(current)];
        const float massScale=float(strength>>7);
        float value=world.mass[std::size_t(current)]/e.maxMass;
        value=value*massScale;
        value=value+(1.0f-massScale);
        value=value*e.strength;
        value=value*float(strength&127);
        value=value*0.25f;
        value=value+(massScale<1.0f?0.10000002384185791f:0.0f);
        resistance=resistance+value;
        if(!std::isfinite(resistance)) { error="Pressure-break resistance overflow."; return false; }
        if(resistance>pressure) { broke=false; return true; }
    }
    if(walls<1 || walls>2 || !(pressure>resistance)) { broke=false; return true; }
    const auto& downstream=elements[id];
    if((downstream.state&3)==2 && world.mass[std::size_t(current)]>0.0f) {
        if(!std::isfinite(world.mass[std::size_t(current)]) || !std::isfinite(downstream.maxMass)||downstream.maxMass<=0) {
            error="Unsupported downstream pressure-break liquid state."; return false;
        }
        pressure=pressure-world.mass[std::size_t(current)]/downstream.maxMass;
    }
    if(!(pressure>resistance)) { broke=false; return true; }
    int32_t source;
    const bool sourceValid=GameCell(world,cell,source);
    std::vector<WorldDamageInfoV744825> pending;
    current=cell;
    for(int step=0;step<walls;++step) {
        current+=offset;
        int32_t destination;
        if(GameCell(world,current,destination)&&sourceValid) pending.push_back({destination,source});
    }
    events.insert(events.end(),pending.begin(),pending.end());
    broke=true; return true;
}

bool CheckLiquidPressureBreakV744825(const LoadedWorldV15& world,
    const std::vector<PressureBreakElementV744825>& elements,
    uint32_t cell, std::vector<WorldDamageInfoV744825>& events, bool& broke, std::string& error) {
    error.clear();
    if(cell>=world.elementIdx.size() || cell>=world.mass.size() || world.elementIdx[cell]>=elements.size()) {
        error="Invalid liquid pressure-break cell."; return false;
    }
    const auto& element=elements[world.elementIdx[cell]];
    if((element.state&3)!=2 || !std::isfinite(element.maxMass)||element.maxMass<=0 ||
        !std::isfinite(world.mass[cell]) || world.mass[cell]<0) {
        error="Invalid liquid pressure-break material state."; return false;
    }
    if(!(world.mass[cell]>element.maxMass)) { broke=false; return true; }
    auto pending=events;
    bool local=false;
    const float pressure=world.mass[cell]/element.maxMass;
    for(int32_t offset:{-int32_t(world.simWidth),-1,1,int32_t(world.simWidth)}) {
        if(!DoPressureBreakV744825(world,elements,cell,pressure,offset,pending,local,error)) return false;
        if(local) { events=std::move(pending); broke=true; return true; }
    }
    broke=false; return true;
}
