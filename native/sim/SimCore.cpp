#include "SimCore.h"
#include "SimWireABI.h"
#include "FrameStepSchedule.h"
#include "StabilityTimers.h"
#include "LiquidFlow.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>
#include <utility>

namespace {
constexpr uint32_t VacuumHash = 0x2D39BF75;
constexpr uint32_t BoundaryHash = 0x6D95058C;
constexpr std::size_t MaxCells = 4 * 1024 * 1024;
uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8)); }
uint32_t U32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
float F32(const uint8_t* p) { uint32_t bits = U32(p); float f; std::memcpy(&f, &bits, 4); return f; }
int32_t I32(const uint8_t* p) { uint32_t bits = U32(p); int32_t n; std::memcpy(&n, &bits, 4); return n; }
void PutU32(std::vector<uint8_t>& out, uint32_t n) {
    for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>(n >> shift));
}
void PutFloat(std::vector<uint8_t>& out, float f) { uint32_t bits; std::memcpy(&bits, &f, 4); PutU32(out, bits); }
bool SizedGrid(uint32_t w, uint32_t h) {
    return w > 0 && h > 0 && w <= 4096 && h <= 4096 &&
        (static_cast<uint64_t>(w) + 2) * (static_cast<uint64_t>(h) + 2) <= MaxCells;
}
bool HasOnlyPadding(const uint8_t* data, std::size_t content, std::size_t bytes) {
    if (bytes != content && bytes != content + 6) return false;
    return std::all_of(data + content, data + bytes, [](uint8_t v) { return v == 0; });
}
void AllocateWorld(LoadedWorldV15& w, uint32_t width, uint32_t height,
                   uint16_t vacuum, uint16_t boundary) {
    w = {};
    w.simWidth = width + 2; w.simHeight = height + 2;
    const std::size_t n = static_cast<std::size_t>(w.simWidth) * w.simHeight;
    w.elementIdx.assign(n, vacuum); w.temperature.assign(n, 0); w.mass.assign(n, 0);
    w.properties.assign(n, 0); w.insulation.assign(n, 255); w.strengthInfo.assign(n, 132);
    w.radiation.assign(n, 0); w.diseaseIdx.assign(n, 255); w.diseaseCount.assign(n, 0);
    w.backwallElement.assign(n, vacuum); w.backwallMass.assign(n, 0); w.backwallTemperature.assign(n, 0);
    for (uint32_t y = 0; y < w.simHeight; ++y) {
        for (uint32_t x = 0; x < w.simWidth; ++x) {
            if (x != 0 && y != 0 && x + 1 != w.simWidth && y + 1 != w.simHeight) continue;
            const std::size_t i = static_cast<std::size_t>(y) * w.simWidth + x;
            w.elementIdx[i] = y + 1 == w.simHeight ? vacuum : boundary;
            w.mass[i] = 9999.0f;
        }
    }
}
template<class T> void ToGame(const LoadedWorldV15& world, const std::vector<T>& source, std::vector<T>& dest) {
    dest.resize(static_cast<std::size_t>(world.GameWidth()) * world.GameHeight());
    for (uint32_t y = 0; y < world.GameHeight(); ++y)
        std::copy_n(source.data() + world.SimIndex(0, y), world.GameWidth(), dest.data() + std::size_t(y) * world.GameWidth());
}

}

bool SimCore::Fail(OniSimStatus status, const char* message) { SetFailure(status, message); return false; }
void SimCore::SetFailure(OniSimStatus status, const char* message) { status_ = status; error_ = message; }
void SimCore::ClearStatus() { status_ = OniSimStatus::Ok; error_.clear(); }
std::size_t SimCore::CellCount() const {
    return phase_ == Phase::GridReady || phase_ == Phase::Started
        ? static_cast<std::size_t>(world_.GameWidth()) * world_.GameHeight() : 0;
}
void SimCore::Initialize(OniSim_GameMessageHandler callback) {
    Shutdown(); callback_ = callback; phase_ = Phase::Initialized; ClearStatus();
}
void SimCore::Shutdown() {
    phase_ = Phase::Cold; callback_ = nullptr; elements_.clear(); diseaseHashes_.clear();
    elementBytes_.clear(); diseaseBytes_.clear(); world_ = {}; view_ = {}; update_ = {};
    vacuum_ = boundary_ = 65535; diseasesReady_ = false; worldOffsetsReady_ = false;
    worldDefinitionPresent_ = false;
    seedKnown_ = false; iterateDirection_ = -1; carrySeconds_ = 0; physicsSteps_ = 0; random_ = {}; pendingPhysicsFrame_ = false;
    pendingSubstance_.clear(); outputSubstance_.clear(); frameFlow_.clear(); flowSourceElements_.clear();
    pendingLiquidChanges_.clear(); outputLiquidChanges_.clear();
    stabilityTimers_.clear();
    radiationEnabled_ = false; allocationOnly_ = false; seed_ = 0; cellValues_ = {}; pendingFrames_ = 0;
    accumulatedFlow_.clear(); textureFlow_.clear(); textureLiquid_.clear();
    textureLiquidData_.clear(); textureMaterial_.clear(); sunlight_.clear(); save_.clear(); saveOpen_ = false;
    ClearStatus();
}

bool SimCore::SetElements(const uint8_t* data, std::size_t bytes) {
    if (phase_ != Phase::Initialized) return Fail(OniSimStatus::InvalidState, "Element table must precede grid allocation.");
    if (bytes < 4) return Fail(OniSimStatus::InvalidInput, "Element table is truncated.");
    const uint32_t count = U32(data);
    if (count == 0 || count >= 65535 || count > (bytes - 4) / 164)
        return Fail(OniSimStatus::InvalidInput, "Element table count or record length is invalid.");
    std::vector<ElementInfo> parsed; parsed.reserve(count);
    std::unordered_set<uint32_t> hashes;
    uint16_t vacuum = 65535, boundary = 65535;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* row = data + 4 + std::size_t(i) * 164;
        const uint32_t hash = U32(row);
        if (U16(row + 8) != i || !hashes.insert(hash).second)
            return Fail(OniSimStatus::InvalidInput, "Element indices/hashes are inconsistent.");
        if (row[11] > 6 || (U16(row + 4) != 65535 && U16(row + 4) >= count) ||
            (U16(row + 6) != 65535 && U16(row + 6) >= count))
            return Fail(OniSimStatus::InvalidInput, "Element render transition or gradient count is invalid.");
        ElementInfo element{};
        element.hash = hash; element.state = row[10] & 3; element.gradientCount = row[11];
        element.lowTransition = U16(row + 4); element.highTransition = U16(row + 6);
        element.lowTemperature = F32(row + 56); element.highTemperature = F32(row + 60);
        element.lightAbsorption = F32(row + 140);
        const float defaultMass = F32(row + 156);
        if (!std::isfinite(element.lowTemperature) || !std::isfinite(element.highTemperature) ||
            element.lowTemperature > element.highTemperature ||
            !std::isfinite((element.highTemperature + 3.0f) - (element.lowTemperature - 3.0f)) ||
            (element.highTemperature + 3.0f) <= (element.lowTemperature - 3.0f))
            return Fail(OniSimStatus::InvalidInput, "Element transition temperatures are outside the verified finite range.");
        if (!std::isfinite(element.lightAbsorption) || element.lightAbsorption < 0 || element.lightAbsorption > 1 ||
            !std::isfinite(defaultMass) || defaultMass < 0)
            return Fail(OniSimStatus::InvalidInput, "Element light absorption or default mass is outside the verified range.");
        element.lightMassScale = element.state == 3 || defaultMass == 0
            ? std::numeric_limits<float>::infinity() : 1.0f / defaultMass;
        element.material = U32(row + 88); element.colour = U32(row + 92);
        for (std::size_t key = 0; key < 6; ++key) element.gradient[key] = U32(row + 96 + key * 4);
        parsed.push_back(element);
        if (hash == VacuumHash) vacuum = static_cast<uint16_t>(i);
        if (hash == BoundaryHash) boundary = static_cast<uint16_t>(i);
    }
    if (vacuum == 65535 || boundary == 65535 || parsed[vacuum].state != 0 || parsed[boundary].state != 3)
        return Fail(OniSimStatus::InvalidInput, "Verified vacuum and boundary elements are required.");
    elements_ = std::move(parsed); elementBytes_.assign(data, data + bytes);
    diseaseHashes_.clear(); diseaseBytes_.clear(); diseasesReady_ = false;
    vacuum_ = vacuum; boundary_ = boundary;
    return true;
}

bool SimCore::SetDiseases(const uint8_t* data, std::size_t bytes) {
    if ((phase_ != Phase::Initialized && !(phase_ == Phase::GridReady && allocationOnly_)) || elements_.empty())
        return Fail(OniSimStatus::InvalidState, "Disease table requires an element table and no allocated grid.");
    if (bytes < 8 || U32(data) > 255 || U32(data + 4) != elements_.size())
        return Fail(OniSimStatus::InvalidInput, "Disease table header does not match the element table.");
    const uint32_t count = U32(data); std::size_t at = 8;
    const std::size_t trailing = 4 + 4 + 4 * 16 + 4 + elements_.size() * 29;
    std::vector<uint32_t> parsed; std::unordered_set<uint32_t> unique;
    for (uint32_t i = 0; i < count; ++i) {
        if (at > bytes || bytes - at < 4) return Fail(OniSimStatus::InvalidInput, "Disease name length is missing.");
        const uint32_t nameBytes = U32(data + at); at += 4;
        if (nameBytes > bytes - at || trailing > bytes - at - nameBytes)
            return Fail(OniSimStatus::InvalidInput, "Disease record exceeds the payload.");
        at += nameBytes; const uint32_t hash = U32(data + at);
        if (hash == 0 || !unique.insert(hash).second)
            return Fail(OniSimStatus::InvalidInput, "Disease hashes are zero or duplicated.");
        parsed.push_back(hash); at += trailing;
    }
    if (at != bytes) return Fail(OniSimStatus::InvalidInput, "Unexpected disease-table trailing bytes.");
    diseaseHashes_ = std::move(parsed); diseaseBytes_.assign(data, data + bytes); diseasesReady_ = true;
    return true;
}

bool SimCore::InitializeCells(const uint8_t* data, std::size_t bytes) {
    if (phase_ != Phase::Initialized || !diseasesReady_)
        return Fail(OniSimStatus::InvalidState, "InitializeFromCells requires both tables and no allocated grid.");
    if (bytes < 14 || !SizedGrid(U32(data), U32(data + 4)) || data[12] > 1 || data[13] > 1)
        return Fail(OniSimStatus::InvalidInput, "InitializeFromCells header or dimensions are invalid.");
    if (data[13] != 1) return Fail(OniSimStatus::UnsupportedFeature, "Only verified headless initialization is supported.");
    const uint32_t width = U32(data), height = U32(data + 4);
    const std::size_t n = std::size_t(width) * height;
    const std::size_t content = 14 + n * 40;
    if (!HasOnlyPadding(data, content, bytes)) return Fail(OniSimStatus::InvalidInput, "InitializeFromCells length/padding differs from the wire contract.");
    LoadedWorldV15 parsed; AllocateWorld(parsed, width, height, vacuum_, boundary_);
    const std::size_t diseaseAt = 14 + n * 16, backwallAt = diseaseAt + n * 12;
    for (std::size_t i = 0; i < n; ++i) {
        const uint8_t* cell = data + 14 + i * 16;
        const uint8_t* disease = data + diseaseAt + i * 12;
        const uint8_t* backwall = data + backwallAt + i * 12;
        const uint16_t element = U16(cell), backwallElement = U16(backwall);
        const float temperature = F32(cell + 8), mass = F32(cell + 12);
        const float backwallMass = F32(backwall + 4), backwallTemperature = F32(backwall + 8);
        const int32_t diseaseCount = I32(disease + 4);
        if (element >= elements_.size() || backwallElement >= elements_.size() ||
            (disease[0] != 255 && disease[0] >= diseaseHashes_.size()) || diseaseCount < 0 ||
            !std::isfinite(temperature) || temperature < 0 || !std::isfinite(mass) || mass < 0 ||
            !std::isfinite(backwallTemperature) || backwallTemperature < 0 ||
            !std::isfinite(backwallMass) || backwallMass < 0)
            return Fail(OniSimStatus::InvalidInput, "InitializeFromCells contains unsupported cell values or indices.");
        const std::size_t dst = parsed.SimIndex(static_cast<uint32_t>(i % width), static_cast<uint32_t>(i / width));
        parsed.elementIdx[dst] = element; parsed.properties[dst] = cell[2]; parsed.insulation[dst] = cell[3];
        parsed.strengthInfo[dst] = cell[4]; parsed.temperature[dst] = temperature; parsed.mass[dst] = mass;
        parsed.diseaseIdx[dst] = disease[0]; parsed.diseaseCount[dst] = diseaseCount;
        parsed.backwallElement[dst] = backwallElement; parsed.backwallMass[dst] = backwallMass;
        parsed.backwallTemperature[dst] = backwallTemperature;
    }
    // Original initialization populates updatedCells, then calls thermal
    // settling with the still-empty cells as SOURCE. No temperatures change
    // in this fresh-grid path (including the thermal-5x7 original fixture).
    // Later positive-time settling needs separate current/updated arrays.
    std::vector<uint8_t> timers(parsed.mass.size(), 31u);
    world_ = std::move(parsed); stabilityTimers_ = std::move(timers);
    seed_ = U32(data + 8); radiationEnabled_ = data[12] != 0;
    random_.seed = seed_; seedKnown_ = true;
    phase_ = Phase::GridReady; worldOffsetsReady_ = true; allocationOnly_ = false;
    return true;
}

bool SimCore::AllocateCells(const uint8_t* data, std::size_t bytes) {
    if (phase_ != Phase::Initialized || elements_.empty()) return Fail(OniSimStatus::InvalidState, "AllocateCells requires an element table.");
    if (bytes != 10 || !SizedGrid(U32(data), U32(data + 4)) || data[8] > 1 || data[9] != 1)
        return Fail(OniSimStatus::InvalidInput, "Only the 10-byte headless AllocateCells payload is supported.");
    AllocateWorld(world_, U32(data), U32(data + 4), vacuum_, boundary_);
    stabilityTimers_.assign(world_.mass.size(), 31u);
    radiationEnabled_ = data[8] != 0; phase_ = Phase::GridReady; allocationOnly_ = true;
    return true;
}

bool SimCore::Load(const uint8_t* data, std::size_t bytes) {
    if (phase_ != Phase::GridReady || !allocationOnly_ || !worldOffsetsReady_ || !diseasesReady_)
        return Fail(OniSimStatus::InvalidState, "Load requires headless allocation and matching element/disease tables.");
    LoadedWorldV15 parsed; std::string error;
    if (bytes < 29) return Fail(OniSimStatus::InvalidInput, "SIMSAVE payload is truncated.");
    if (data[28] != 0)
        return Fail(OniSimStatus::UnsupportedFeature, "Only the verified zero SIMSAVE option byte is supported.");
    if (!DecodeSaveWorldV15(std::vector<uint8_t>(data, data + bytes), elementBytes_, diseaseBytes_, parsed, error)) {
        status_ = OniSimStatus::UnsupportedFeature; error_ = error; return false;
    }
    if (parsed.simWidth != world_.simWidth || parsed.simHeight != world_.simHeight)
        return Fail(OniSimStatus::InvalidInput, "Load dimensions differ from the allocated grid.");
    for (std::size_t cell = 0; cell < parsed.mass.size(); ++cell) {
        if (!std::isfinite(parsed.temperature[cell]) || parsed.temperature[cell] < 0 ||
            !std::isfinite(parsed.mass[cell]) || parsed.mass[cell] < 0 ||
            !std::isfinite(parsed.radiation[cell]) || parsed.radiation[cell] < 0 ||
            parsed.diseaseCount[cell] < 0 ||
            !std::isfinite(parsed.backwallTemperature[cell]) || parsed.backwallTemperature[cell] < 0 ||
            !std::isfinite(parsed.backwallMass[cell]) || parsed.backwallMass[cell] < 0)
            return Fail(OniSimStatus::UnsupportedFeature, "Load contains cell values outside the verified finite range.");
    }
    for (auto& backwall : parsed.backwallElement) if (backwall == 65535) backwall = vacuum_;
    world_ = std::move(parsed); worldOffsetsReady_ = true; allocationOnly_ = false;
    return true;
}

void SimCore::RefreshCells(bool initial) {
    ToGame(world_, world_.elementIdx, view_.elementIdx); ToGame(world_, world_.temperature, view_.temperature);
    ToGame(world_, world_.mass, view_.mass); ToGame(world_, world_.properties, view_.properties);
    ToGame(world_, world_.insulation, view_.insulation); ToGame(world_, world_.strengthInfo, view_.strengthInfo);
    ToGame(world_, world_.radiation, view_.radiation); ToGame(world_, world_.diseaseIdx, view_.diseaseIdx);
    ToGame(world_, world_.diseaseCount, view_.diseaseCount);
    if (initial) {
        view_.backwallElement.assign(CellCount(), 65535); view_.backwallMass.assign(CellCount(), 0);
        view_.backwallTemperature.assign(CellCount(), 0);
    } else {
        ToGame(world_, world_.backwallElement, view_.backwallElement); ToGame(world_, world_.backwallMass, view_.backwallMass);
        ToGame(world_, world_.backwallTemperature, view_.backwallTemperature);
    }
}

void SimCore::RefreshSunlight() {
    // Original UpdateExposedToSunPropertyTexture walks the defined worlds.
    // InitializeFromCells alone leaves that list empty and the buffer unchanged.
    if (!worldDefinitionPresent_) return;
    const uint32_t width = world_.GameWidth(), height = world_.GameHeight();
    std::fill_n(sunlight_.data() + std::size_t(height - 1) * width, width, uint8_t(255));
    std::vector<float> light(width, 1.0f);
    const auto glass = std::find_if(elements_.begin(), elements_.end(), [](const ElementInfo& element) {
        return element.hash == 0x2531469C;
    });
    for (uint32_t row = height - 1; row > 0; --row) for (uint32_t x = 0; x < width; ++x) {
        const std::size_t game = std::size_t(row - 1) * width + x;
        const std::size_t cell = world_.SimIndex(x, row - 1), above = world_.SimIndex(x, row);
        const uint8_t properties = world_.properties[cell];
        const bool window = (properties & 0x30) == 0x30;
        const auto& element = window ? *glass : elements_[world_.elementIdx[above]];
        // fmin also reproduces MINSS's second-operand result for 0 * infinity.
        const float absorption = std::fmin(world_.mass[above] * element.lightMassScale, 1.0f) * element.lightAbsorption;
        const float exposed = std::max(light[x] - absorption, 0.0f);
        light[x] = !window && (properties & 0x20) ? 0.0f : exposed;
        sunlight_[game] = static_cast<uint8_t>(exposed * 255.0f);
    }
}

bool SimCore::RefreshTextures() {
    // The verified initialization/zero-time path has zero flow.
    // This reconstructs the three CPU liquid buffers, not positive-time light
    // propagation, flow or a GPU texture API.
    const std::size_t width = world_.GameWidth(), count = CellCount();
    constexpr float inv255 = 1.0f / 255.0f;
    auto clamp = [](float value) { return std::max(0.0f, std::min(1.0f, value)); };
    auto massFactor = [&](std::size_t cell) { return std::pow(std::min(view_.mass[cell] / 1000.0f, 1.0f), 0.45f); };
    auto localGradient = [&](std::size_t cell) {
        const auto& element = elements_[view_.elementIdx[cell]];
        if (element.state <= 1) return 0.0f;
        if (element.state > 2) return 1.0f;
        const auto& above = cell + width < count ? elements_[view_.elementIdx[cell + width]] : elements_[vacuum_];
        if (above.state <= 1) return 0.0f;
        return clamp(1.0f - static_cast<float>(sunlight_[cell]) * inv255) * massFactor(cell);
    };
    auto put = [](std::vector<uint8_t>& buffer, std::size_t cell, uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte) buffer[cell * 4 + byte] = static_cast<uint8_t>(value >> (byte * 8));
    };
    auto liquid = textureLiquid_, liquidData = textureLiquidData_, material = textureMaterial_;
    // Native loops stop before the last row; preserve its previous bytes.
    for (std::size_t cell = 0; cell < count - width; ++cell) {
        const auto& element = elements_[view_.elementIdx[cell]];
        uint32_t colourOut = 0, dataOut = 0, materialOut = 0;
        if (element.state == 2) {
            float gradient = localGradient(cell);
            if (gradient > 0) gradient = std::min(gradient, localGradient(cell + width));
            uint32_t colour = element.colour & 0xFFFFFF;
            for (uint8_t key = 0; key < element.gradientCount; ++key) {
                const uint32_t base = element.gradient[key];
                const float begin = static_cast<float>(base >> 24) * inv255;
                if (gradient < begin) continue;
                const uint32_t following = element.gradient[std::min<int>(key + 1, element.gradientCount - 1)];
                const float end = static_cast<float>(following >> 24) * inv255;
                const float factor = end != begin ? clamp((gradient - begin) / (end - begin)) : 0.0f;
                colour = 0;
                for (unsigned shift = 0; shift < 24; shift += 8) {
                    const int32_t first = static_cast<int32_t>((base >> shift) & 255);
                    const int32_t second = static_cast<int32_t>((following >> shift) & 255);
                    const int32_t channel = first + static_cast<int32_t>(static_cast<float>(second - first) * factor);
                    colour |= static_cast<uint32_t>(channel) << shift;
                }
            }
            const auto& above = elements_[view_.elementIdx[cell + width]];
            const uint32_t alpha = above.state >= 2 || (view_.properties[cell + width] & 2) ? 255u
                : static_cast<uint32_t>(std::pow(std::min(view_.mass[cell] * 0.001f, 1.0f), 0.45f) * 255.0f);
            colourOut = (colour & 0xFFFFFF) | (alpha << 24);
            const float low = element.lowTemperature - 3.0f, high = element.highTemperature + 3.0f;
            const float normalized = clamp((view_.temperature[cell] - low) / (high - low));
            if (!std::isfinite(normalized) || !std::isfinite(gradient))
                return Fail(OniSimStatus::UnsupportedFeature, "Liquid interpolation exceeds the verified finite range.");
            const uint16_t transition = normalized >= 0.5f ? element.highTransition : element.lowTransition;
            const uint32_t transitionColour = transition == 65535 ? 0xFFFFFF : elements_[transition].colour;
            dataOut = (transitionColour & 0xFFFFFF) | (static_cast<uint32_t>(normalized * 255.0f) << 24);
            materialOut = element.material & 0xFF00000F;
        }
        put(liquid, cell, colourOut); put(liquidData, cell, dataOut); put(material, cell, materialOut);
    }
    textureLiquid_ = std::move(liquid); textureLiquidData_ = std::move(liquidData); textureMaterial_ = std::move(material);
    return true;
}

void SimCore::BindOutput() {
    update_.elementIdx = view_.elementIdx.data(); update_.temperature = view_.temperature.data();
    update_.mass = view_.mass.data(); update_.properties = view_.properties.data(); update_.insulation = view_.insulation.data();
    update_.strengthInfo = view_.strengthInfo.data(); update_.radiation = view_.radiation.data();
    update_.diseaseIdx = view_.diseaseIdx.data(); update_.diseaseCount = view_.diseaseCount.data();
    update_.backwallElement = view_.backwallElement.data(); update_.backwallMass = view_.backwallMass.data();
    update_.backwallTemperature = view_.backwallTemperature.data(); update_.accumulatedFlow = accumulatedFlow_.data();
    update_.propertyTextureFlow = textureFlow_.data(); update_.propertyTextureLiquid = textureLiquid_.data();
    update_.propertyTextureLiquidData = textureLiquidData_.data(); update_.propertyTextureMaterialData = textureMaterial_.data();
    update_.propertyTextureExposedToSunlight = sunlight_.data();
    update_.numSubstanceChangeInfo = static_cast<int32_t>(outputSubstance_.size());
    update_.substanceChangeInfo = outputSubstance_.empty() ? nullptr : outputSubstance_.data();
    update_.numLiquidChangeInfo = static_cast<int32_t>(outputLiquidChanges_.size());
    update_.liquidChangeInfo = outputLiquidChanges_.empty() ? nullptr : outputLiquidChanges_.data();
}

void SimCore::GetDiagnostics(OniSimDiagnostics* state) {
    ClearStatus();
    if (state == nullptr) { Fail(OniSimStatus::InvalidInput, "Diagnostic output pointer is null."); return; }
    if (phase_ == Phase::Cold) { Fail(OniSimStatus::InvalidState, "The native core is not initialized."); return; }
    *state = {random_.seed, iterateDirection_, random_.displacementDirection, carrySeconds_, physicsSteps_};
}

void SimCore::RefreshFlow() {
    if (frameFlow_.empty()) return;
    for (uint32_t y = 0; y < world_.GameHeight(); ++y) for (uint32_t x = 0; x < world_.GameWidth(); ++x) {
        const std::size_t game = std::size_t(y) * world_.GameWidth() + x, sim = world_.SimIndex(x, y);
        const auto& flow = frameFlow_[sim];
        const float scale = flowSourceElements_[sim] == world_.elementIdx[sim] ? 1.0f / std::max(world_.mass[sim], 1.0f) : 0.0f;
        const float velocity[] = {(flow[0] - flow[1]) * scale, (flow[3] - flow[2]) * scale};
        std::memcpy(textureFlow_.data() + game * 8, velocity, sizeof(velocity));
    }
}

int32_t SimCore::CopyStabilityTimers(uint8_t* destination, int32_t length) {
    ClearStatus();
    if ((phase_ != Phase::GridReady && phase_ != Phase::Started) || stabilityTimers_.size() != world_.mass.size()) {
        Fail(OniSimStatus::InvalidState, "Timer diagnostics require an allocated grid."); return 0;
    }
    const int32_t bytes = static_cast<int32_t>(stabilityTimers_.size());
    if (destination == nullptr && length == 0) return bytes;
    if (destination == nullptr || length != bytes) {
        Fail(OniSimStatus::InvalidInput, "Timer output buffer must match the internal grid length."); return 0;
    }
    std::memcpy(destination, stabilityTimers_.data(), stabilityTimers_.size());
    return bytes;
}

bool SimCore::AdvanceFluidFrame(const NewGameFrame& frame, LoadedWorldV15& world) {
    if (radiationEnabled_ || !seedKnown_ || frame.currentCosmicRadiationIntensity != 0)
        return Fail(OniSimStatus::UnsupportedFeature, "Positive frames currently require non-radiating InitializeFromCells with a known seed.");
    if (pendingFrames_ != 0)
        return Fail(OniSimStatus::InvalidState, "PrepareGameData must consume the preceding frame before a positive frame.");
    std::vector<PressureElementV744825> pressure;
    std::vector<GasPostElementV744825> post;
    uint16_t vacuum = 65535; std::string error;
    if (!DecodePressureElementsV744825(elementBytes_, pressure, vacuum, error) ||
        !DecodeGasPostElementsV744825(elementBytes_, post, error)) {
        status_ = OniSimStatus::UnsupportedFeature; error_ = error; return false;
    }
    std::vector<LiquidElementV744825> liquids; uint16_t water = 65535, liquidVacuum = 65535;
    if (!DecodeLiquidElementsV744825(elementBytes_, liquids, water, liquidVacuum, error) || liquidVacuum != vacuum) {
        status_ = OniSimStatus::UnsupportedFeature; error_ = error; return false;
    }
    bool waterWorld = false, hasGas = false;
    for (uint16_t id : world.elementIdx) {
        if (pressure[id].state == 2) {
            if (id != water) return Fail(OniSimStatus::UnsupportedFeature, "Only the verified water liquid is supported.");
            waterWorld = true;
        }
        hasGas = hasGas || pressure[id].state == 1;
    }
    if (waterWorld && hasGas) return Fail(OniSimStatus::UnsupportedFeature, "Water/gas interaction is not implemented.");
    // The original temperature task skips differences < 1 K and materials
    // with no conductivity or the insulation state bit. This narrow gas band
    // permits pressure rounding without pretending to implement heat exchange.
    if (F32(elementBytes_.data() + 4 + std::size_t(vacuum) * 164 + 16) != 0 ||
        (post[boundary_].state & 0x10) == 0)
        return Fail(OniSimStatus::UnsupportedFeature, "Thermal exchange with vacuum or the boundary is outside this gas path.");
    for (std::size_t cell = 0; cell < world.mass.size(); ++cell) {
        if (world.radiation[cell] != 0 || world.backwallMass[cell] != 0 ||
            world.backwallTemperature[cell] != 0 || world.backwallElement[cell] != vacuum ||
            ((pressure[world.elementIdx[cell]].state == 1 || pressure[world.elementIdx[cell]].state == 2) &&
                std::abs(world.temperature[cell] - 300.0f) >= 0.25f))
            return Fail(OniSimStatus::UnsupportedFeature, "Positive fluid frames require fluid within 0.25 K of 300 K, zero radiation and empty backwalls.");
    }
    float carry = carrySeconds_; FrameStepPlan plan{};
    if (!PlanFrameSteps(frame.elapsedSeconds, carry, plan) || plan.updateDataCalls > 100)
        return Fail(OniSimStatus::UnsupportedFeature, "Frame timestep is outside the supported bounded gas schedule.");
    PressureRegionV744825 region{static_cast<uint32_t>(frame.activeRegion.minX), static_cast<uint32_t>(frame.activeRegion.minY),
        static_cast<uint32_t>(frame.activeRegion.maxX), static_cast<uint32_t>(frame.activeRegion.maxY)};
    auto random = random_; int direction = iterateDirection_;
    std::vector<std::array<float, 4>> flow(world.mass.size());
    std::vector<uint32_t> notified;
    LoadedWorldV15 current = world;
    std::vector<uint16_t> flowSource;
    for (int step = 0; step < plan.updateDataCalls; ++step) {
        LoadedWorldV15 updated; std::vector<uint32_t> changes;
        direction = -direction;
        if (!StepGasPressureV744825(current, pressure, vacuum, region, direction, updated, changes, error, &flow)) {
            status_ = OniSimStatus::UnsupportedFeature; error_ = error; return false;
        }
        notified.insert(notified.end(), changes.begin(), changes.end());
        if (waterWorld) {
            current = std::move(updated);
            if (!StepWaterFlowV744825(current, liquids, water, vacuum, region, updated, changes, flow, error)) {
                status_ = OniSimStatus::UnsupportedFeature; error_ = error; return false;
            }
            notified.insert(notified.end(), changes.begin(), changes.end());
        }
        // UpdateData copies updatedCells to cells after pressure and before
        // PostProcessCell; the flow renderer compares that snapshot to final.
        flowSource = updated.elementIdx;
        current = std::move(updated);
        const bool processed = waterWorld ?
            StepWaterPostProcessV744825(current, liquids, post, water, vacuum, region, random, updated, changes, flow, error) :
            StepGasPostProcessV744825(current, post, region, random, updated, changes, error, &flow);
        if (!processed) {
            status_ = OniSimStatus::UnsupportedFeature; error_ = error; return false;
        }
        notified.insert(notified.end(), changes.begin(), changes.end());
        current = std::move(updated);
    }
    auto timers = stabilityTimers_;
    for (uint32_t cell : notified) ResetStableCellTimerV744825(timers[cell]);
    std::sort(notified.begin(), notified.end());
    notified.erase(std::unique(notified.begin(), notified.end()), notified.end());
    std::vector<SubstanceNotification> events;
    std::vector<int32_t> liquidChanges;
    for (uint32_t cell : notified) {
        const uint32_t sx = cell % current.simWidth, sy = cell / current.simWidth;
        if (sx == 0 || sy == 0 || sx + 1 == current.simWidth || sy + 1 == current.simHeight) continue;
        const std::size_t game = std::size_t(sy - 1) * current.GameWidth() + sx - 1;
        events.push_back({static_cast<int32_t>(game), view_.elementIdx[game], current.elementIdx[cell]});
        if (view_.elementIdx[game] != current.elementIdx[cell] &&
            (pressure[view_.elementIdx[game]].state == 2 || pressure[current.elementIdx[cell]].state == 2))
            liquidChanges.push_back(static_cast<int32_t>(game));
    }
    world = std::move(current); frameFlow_ = std::move(flow); flowSourceElements_ = std::move(flowSource);
    pendingSubstance_ = std::move(events); random_ = random; iterateDirection_ = direction;
    pendingLiquidChanges_ = std::move(liquidChanges);
    stabilityTimers_ = std::move(timers);
    carrySeconds_ = carry; physicsSteps_ += static_cast<uint64_t>(plan.updateDataCalls);
    return true;
}

bool SimCore::QueueFrame(const uint8_t* data, std::size_t bytes) {
    if (phase_ != Phase::Started) return Fail(OniSimStatus::InvalidState, "NewGameFrame requires Start.");
    if (pendingPhysicsFrame_) return Fail(OniSimStatus::InvalidState, "PrepareGameData must consume the pending positive frame.");
    if (bytes != sizeof(NewGameFrame)) return Fail(OniSimStatus::UnsupportedFeature, "Only one active frame region is supported.");
    NewGameFrame frame{}; std::memcpy(&frame, data, sizeof(frame));
    if (frame.activeRegion.minX < 0 || frame.activeRegion.minY < 0 || frame.activeRegion.maxX < frame.activeRegion.minX ||
        frame.activeRegion.maxY < frame.activeRegion.minY || uint32_t(frame.activeRegion.maxX) > world_.GameWidth() ||
        uint32_t(frame.activeRegion.maxY) > world_.GameHeight()) return Fail(OniSimStatus::InvalidInput, "Frame region is invalid.");
    if (pendingFrames_ == std::numeric_limits<int32_t>::max()) return Fail(OniSimStatus::InvalidState, "Pending frame count exceeds the wire integer range.");
    std::vector<uint8_t> insulation, strength;
    ToGame(world_, world_.insulation, insulation); ToGame(world_, world_.strengthInfo, strength);
    LoadedWorldV15 candidate = world_;
    auto queue = cellValues_;
    if (!queue.ApplyInsulation(insulation.data(), CellCount()) || !queue.ApplyStrength(strength.data(), CellCount()))
        return Fail(OniSimStatus::InvalidInput, "A queued cell-value message addresses an invalid game cell.");
    for (uint32_t y = 0; y < world_.GameHeight(); ++y) for (uint32_t x = 0; x < world_.GameWidth(); ++x) {
        const std::size_t g = std::size_t(y) * world_.GameWidth() + x, s = world_.SimIndex(x, y);
        candidate.insulation[s] = insulation[g]; candidate.strengthInfo[s] = strength[g];
    }
    if (frame.elapsedSeconds > 0 && !AdvanceFluidFrame(frame, candidate)) return false;
    if (frame.elapsedSeconds > 0) pendingPhysicsFrame_ = true;
    if (!(frame.elapsedSeconds > 0)) {
        std::vector<std::array<float, 4>> flow(candidate.mass.size());
        auto sourceElements = candidate.elementIdx;
        frameFlow_ = std::move(flow); flowSourceElements_ = std::move(sourceElements);
    }
    world_ = std::move(candidate);
    cellValues_ = std::move(queue); ++pendingFrames_;
    return true;
}

void* SimCore::HandleMessage(int32_t id, int32_t bytes, const uint8_t* data) {
    ClearStatus();
    if (phase_ == Phase::Cold) { Fail(OniSimStatus::InvalidState, "The native core is not initialized."); return nullptr; }
    if (bytes < 0 || (bytes > 0 && data == nullptr)) { Fail(OniSimStatus::InvalidInput, "Payload length/pointer is invalid."); return nullptr; }
    if (saveOpen_) { Fail(OniSimStatus::InvalidState, "EndSave must precede another message."); return nullptr; }
    const auto length = static_cast<std::size_t>(bytes);
    switch (id) {
    case 1108437482: SetElements(data, length); return nullptr;
    case 825301935: SetDiseases(data, length); return nullptr;
    case 2062421945: return InitializeCells(data, length) ? reinterpret_cast<void*>(CellCount()) : nullptr;
    case 1092408308: AllocateCells(data, length); return nullptr;
    case -895846551:
        if (phase_ != Phase::GridReady || length != 20 || U32(data) != 1 || U32(data + 4) != 0 ||
            U32(data + 8) != 0 || U32(data + 12) != world_.GameWidth() || U32(data + 16) != world_.GameHeight())
            Fail(OniSimStatus::UnsupportedFeature, "Only one world at zero offsets spanning the whole grid is supported.");
        else {
            if (std::none_of(elements_.begin(), elements_.end(), [](const ElementInfo& element) {
                return element.hash == 0x2531469C;
            }))
                Fail(OniSimStatus::UnsupportedFeature, "World sunlight requires the verified Glass element.");
            else { worldOffsetsReady_ = true; worldDefinitionPresent_ = true; }
        }
        return nullptr;
    case -1836204275:
        if (phase_ != Phase::GridReady || !allocationOnly_ || !worldOffsetsReady_ || length != 0)
            Fail(OniSimStatus::UnsupportedFeature, "ClearUnoccupiedCells requires a full-grid single-world definition.");
        return nullptr;
    case -672538170: return Load(data, length) ? &update_ : nullptr;
    case -931446686:
        if (phase_ != Phase::GridReady || allocationOnly_ || !worldOffsetsReady_ || !diseasesReady_ || length != 0) {
            Fail(OniSimStatus::InvalidState, "Start requires a complete grid and tables, and an empty payload."); return nullptr;
        }
        RefreshCells(true); accumulatedFlow_.assign(CellCount(), 0);
        textureFlow_.assign(CellCount() * 8, 0); textureLiquid_.assign(CellCount() * 4, 0);
        textureLiquidData_.assign(CellCount() * 4, 0); textureMaterial_.assign(CellCount() * 4, 0); sunlight_.assign(CellCount(), 0);
        phase_ = Phase::Started; BindOutput(); return &update_;
    case 1078620451:
        if (phase_ != Phase::Started || length != CellCount()) {
            Fail(OniSimStatus::InvalidInput, "PrepareGameData requires exactly one visibility byte per game cell after Start."); return nullptr;
        }
        RefreshCells(false);
        RefreshFlow();
        RefreshSunlight();
        if (!RefreshTextures()) return nullptr;
        outputSubstance_ = std::move(pendingSubstance_); pendingSubstance_.clear();
        outputLiquidChanges_ = std::move(pendingLiquidChanges_); pendingLiquidChanges_.clear();
        update_.numFramesProcessed = pendingFrames_; pendingFrames_ = 0; pendingPhysicsFrame_ = false; BindOutput(); return &update_;
    case -775326397: QueueFrame(data, length); return nullptr;
    case FrameCellValues::SetInsulationValueMessageId:
    case FrameCellValues::SetStrengthValueMessageId:
        return HandleMessages(id, bytes, 1, data);
    default: Fail(OniSimStatus::UnsupportedMessage, "This simulation message is not implemented."); return nullptr;
    }
}

void* SimCore::HandleMessages(int32_t id, int32_t bytes, int32_t count, const uint8_t* data) {
    ClearStatus();
    if (phase_ != Phase::Started || saveOpen_) { Fail(OniSimStatus::InvalidState, "Cell messages require an active core outside a save operation."); return nullptr; }
    if (bytes != 8 || count < 0 || count > 1024 * 1024 || (count > 0 && data == nullptr)) {
        Fail(OniSimStatus::InvalidInput, "Cell-value batch length/count/pointer is invalid."); return nullptr;
    }
    if (id != FrameCellValues::SetInsulationValueMessageId && id != FrameCellValues::SetStrengthValueMessageId) {
        Fail(OniSimStatus::UnsupportedMessage, "This batched message is not implemented."); return nullptr;
    }
    auto pending = cellValues_;
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* row = data + std::size_t(i) * 8;
        if (I32(row) < 0 || std::size_t(I32(row)) >= CellCount()) {
            Fail(OniSimStatus::InvalidInput, "Cell-value batch contains an invalid game-cell index."); return nullptr;
        }
        if (id == FrameCellValues::SetInsulationValueMessageId) pending.EnqueueInsulation(row, 8);
        else pending.EnqueueStrength(row, 8);
    }
    cellValues_ = std::move(pending); return nullptr;
}

uint8_t* SimCore::BeginSave(int32_t* bytes, int32_t x, int32_t y) {
    ClearStatus(); if (bytes != nullptr) *bytes = 0;
    if (bytes == nullptr || x != 0 || y != 0) { Fail(OniSimStatus::InvalidInput, "Save requires an output size and zero offsets."); return nullptr; }
    if ((phase_ != Phase::GridReady && phase_ != Phase::Started) || allocationOnly_ || saveOpen_ || !diseasesReady_) {
        Fail(OniSimStatus::InvalidState, "Save requires an initialized world outside an existing save operation."); return nullptr;
    }
    std::vector<uint8_t> out; const std::size_t n = world_.mass.size(); out.reserve(29 + 36 * n);
    const uint8_t signature[8] = {'S','I','M','S','A','V','E',0}; out.insert(out.end(), signature, signature + 8);
    PutU32(out, 15); PutU32(out, world_.simWidth); PutU32(out, world_.simHeight); PutU32(out, 0); PutU32(out, 0);
    out.push_back(0); // Verified option byte in both radiation-enabled/disabled reference saves.
    for (std::size_t i = 0; i < n; ++i) {
        PutU32(out, elements_[world_.elementIdx[i]].hash); PutFloat(out, world_.temperature[i]);
        PutFloat(out, world_.mass[i]); PutFloat(out, world_.radiation[i]);
    }
    for (std::size_t i = 0; i < n; ++i) {
        PutU32(out, world_.diseaseIdx[i] == 255 ? 0 : diseaseHashes_[world_.diseaseIdx[i]]);
        PutU32(out, static_cast<uint32_t>(world_.diseaseCount[i]));
    }
    for (std::size_t i = 0; i < n; ++i) {
        PutU32(out, elements_[world_.backwallElement[i]].hash); PutFloat(out, world_.backwallMass[i]);
        PutFloat(out, world_.backwallTemperature[i]);
    }
    save_ = std::move(out); saveOpen_ = true; *bytes = static_cast<int32_t>(save_.size()); return save_.data();
}
void SimCore::EndSave() { ClearStatus(); save_.clear(); saveOpen_ = false; }
