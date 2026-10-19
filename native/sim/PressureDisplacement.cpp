#include "PressureDisplacement.h"
#include "StabilityTimers.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

bool DisplaceOverpressuredWaterV744825(LoadedWorldV15& world,
    const std::vector<LiquidElementV744825>& elements,
    uint16_t water, uint16_t vacuum, uint32_t cell,
    std::vector<uint32_t>& changes, std::vector<uint8_t>& timers,
    bool& displaced, std::string& error, uint16_t tickCount) {
    error.clear();
    const std::size_t count=std::size_t(world.simWidth)*world.simHeight;
    if(world.simWidth<3 || world.simHeight<3 || world.elementIdx.size()!=count ||
        world.mass.size()!=count || world.temperature.size()!=count ||
        world.properties.size()!=count || world.diseaseIdx.size()!=count ||
        world.diseaseCount.size()!=count || timers.size()!=count || cell>=count ||
        water>=elements.size() || vacuum>=elements.size() || elements[water].hash!=1836671383u ||
        elements[vacuum].hash!=0x2D39BF75u || world.elementIdx[cell]!=water ||
        !std::isfinite(elements[water].maxMass) || elements[water].maxMass<=0 ||
        !std::isfinite(world.mass[cell]) || world.mass[cell]<0) {
        error="Invalid overpressure displacement grid, liquid or cell."; return false;
    }
    // Original 0x4DD68 uses 1.5, not three times maxMass. No clone or
    // all-world scan is needed on these original no-op paths.
    const float mass=world.mass[cell], maximum=elements[water].maxMass;
    if(!(mass>maximum*1.5f)) { displaced=false; return true; }
    const std::size_t above=std::size_t(cell)+world.simWidth;
    if(above>=count || world.elementIdx[above]>=elements.size() ||
        !std::isfinite(world.mass[above]) || world.mass[above]<0) {
        error="Overpressure displacement cannot read the cell above."; return false;
    }
    if(!(mass>world.mass[above]) || (elements[world.elementIdx[above]].state&3u)!=2u) {
        displaced=false; return true;
    }
    if(world.elementIdx[above]!=water) { error="Overpressure displacement supports only Water liquid."; return false; }
    if(!(mass>std::max(world.mass[above]*1.01f,maximum)) || !(world.mass[above]>0.0f)) {
        displaced=false; return true;
    }
    const bool trace=world.mass[above]<0.01f;
    if(trace) {
        bool possibleGas=false;
        for(const std::size_t n:{above+1,above-1,above+world.simWidth,above-world.simWidth}) {
            if(n>=count || world.elementIdx[n]>=elements.size()) { error="Invalid sub-.01 liquid displacement neighbor."; return false; }
            possibleGas |= (elements[world.elementIdx[n]].state&3u)==1 && world.mass[n]>0;
        }
        if(!possibleGas) { displaced=false; return true; }
    }
    const auto Interior=[&](std::size_t n) {
        return n>=world.simWidth && n+world.simWidth<count && n%world.simWidth!=0 && n%world.simWidth+1!=world.simWidth;
    };
    if(!Interior(cell)||!Interior(above)) { error="Unsupported overpressure displacement border source."; return false; }
    for(std::size_t n=0;n<count;++n) {
        if(world.elementIdx[n]>=elements.size() || !std::isfinite(world.mass[n]) || world.mass[n]<0 ||
            world.mass[n]>1.0e10f || !std::isfinite(world.temperature[n]) || world.temperature[n]<0 ||
            world.temperature[n]>10000 || (world.properties[n]&~0x40u)!=0 ||
            world.diseaseIdx[n]!=255 || world.diseaseCount[n]!=0 ||
            ((elements[world.elementIdx[n]].state&3u)==2u && world.elementIdx[n]!=water)) {
            error="Overpressure displacement requires bounded Water cells without disease or unknown properties."; return false;
        }
    }
    const std::size_t candidates[]{above+1,above-1,above+world.simWidth,above-world.simWidth};
    std::vector<std::size_t> targets;
    if(!trace) for(const auto n:candidates) {
        if(n>=count) { error="Overpressure displacement target leaves grid."; return false; }
        if(world.elementIdx[n]==water || world.elementIdx[n]==vacuum) {
            targets.push_back(n);
        }
    }
    if(!trace && targets.empty()) { error="Overpressure displacement has no verified Water recipient."; return false; }
    LoadedWorldV15 next=world;
    auto nextTimers=timers;
    auto notifications=changes;
    auto Notify=[&](std::size_t n) {
        // ChangeSubstance always resets the internal timer, but appends its
        // event only when the native linear game-cell conversion is in range.
        const int64_t gameCell=(int64_t(n/world.simWidth)-1)*int64_t(world.simWidth-2)+int64_t(n%world.simWidth)-1;
        if(gameCell>=0 && gameCell<int64_t(world.GameWidth())*world.GameHeight())
            notifications.push_back(static_cast<uint32_t>(n));
        ResetStableCellTimerV744825(nextTimers[n]);
    };
    auto Add=[&](std::size_t n,float amount,float fromTemperature,uint16_t id) {
        const float previous=next.mass[n],total=previous+amount,oldTemperature=next.temperature[n];
        float temperature=(previous*oldTemperature+fromTemperature*amount)/total;
        temperature=std::max(std::min(temperature,std::max(oldTemperature,fromTemperature)),std::min(oldTemperature,fromTemperature));
        if(!std::isfinite(total)||!std::isfinite(temperature)) { error="Overpressure displacement arithmetic overflow."; return false; }
        next.mass[n]=total;next.temperature[n]=temperature;next.elementIdx[n]=id;return true;
    };
    if(trace) {
        bool movedGas=false;
        for(unsigned i=0;i<4;++i) {
            const std::size_t gasCell=candidates[(tickCount+i)&3u];
            if(gasCell>=count || next.elementIdx[gasCell]>=elements.size()) { error="Invalid trace liquid gas candidate."; return false; }
            if((elements[next.elementIdx[gasCell]].state&3u)!=1 || !(next.mass[gasCell]>0)) continue;
            if(!Interior(gasCell)) { error="Trace-liquid gas displacement at border is not verified."; return false; }
            const uint16_t id=next.elementIdx[gasCell];
            const std::array<std::size_t,4> gasTargets{gasCell+world.simWidth,gasCell-1,gasCell+1,gasCell-world.simWidth};
            std::size_t destination=count;
            for(unsigned k=0;k<4;++k) {
                const auto n=gasTargets[(tickCount+k)&3u];
                if(n>=count) { error="Trace-liquid gas destination leaves grid."; return false; }
                if(next.elementIdx[n]==id || next.elementIdx[n]==vacuum) { destination=n;break; }
            }
            if(destination==count) {
                const std::array<std::size_t,2> diagonal{gasCell+world.simWidth-1,gasCell+world.simWidth+1};
                const std::array<std::size_t,2> sides{gasCell-1,gasCell+1};
                for(unsigned k=0;k<2;++k) {
                    const unsigned q=(tickCount+k)&1u;
                    if(diagonal[q]>=count) { error="Trace-liquid diagonal gas destination leaves grid."; return false; }
                    if(next.elementIdx[diagonal[q]]==id && (elements[next.elementIdx[sides[q]]].state&3u)!=3) {
                        destination=diagonal[q];break;
                    }
                }
            }
            if(destination==count) continue;
            if(!Add(destination,next.mass[gasCell],next.temperature[gasCell],id)) return false;
            next.elementIdx[gasCell]=vacuum;next.mass[gasCell]=next.temperature[gasCell]=0;
            next.diseaseIdx[gasCell]=255;next.diseaseCount[gasCell]=0;
            Notify(gasCell);Notify(destination);movedGas=true;break;
        }
        if(movedGas) { world=std::move(next);timers=std::move(nextTimers);changes=std::move(notifications); }
        // The retry remains below .01 and returns false, preserving a preceding
        // successful gas displacement. No upper-liquid mass was consumed.
        displaced=false;return true;
    }
    const float amount=world.mass[above]/float(targets.size());
    const float fromTemperature=world.temperature[above];
    for(const auto n:targets) {
        if(!Add(n,amount,fromTemperature,water)) return false;
        Notify(n);
    }
    next.elementIdx[above]=vacuum;
    next.mass[above]=next.temperature[above]=0;
    next.diseaseIdx[above]=255; next.diseaseCount[above]=0;
    Notify(above);
    // Native constant RVA 0xD2190, IEEE binary32 0x3EFEB9F3. The source
    // may already have received a share of the displaced upper-cell mass.
    const float transported=next.mass[cell]*0.49751242995262146f;
    next.mass[above]=transported;
    next.temperature[above]=next.temperature[cell];
    next.elementIdx[above]=next.elementIdx[cell];
    next.diseaseIdx[above]=next.diseaseIdx[cell]; next.diseaseCount[above]=0;
    Notify(above);
    next.mass[cell]=next.mass[cell]-transported;
    // Original clears source disease when its resulting count is <= zero.
    next.diseaseIdx[cell]=255; next.diseaseCount[cell]=0;
    world=std::move(next); timers=std::move(nextTimers); changes=std::move(notifications);
    displaced=true; return true;
}
