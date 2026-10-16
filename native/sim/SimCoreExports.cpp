#include "SimCore.h"
#include <cstdio>
#include <exception>
#include <mutex>
#include <new>

namespace {
SimCore core;
std::mutex coreMutex;
thread_local int32_t lastStatus = 0;
thread_local char lastError[512]{};
void Report(OniSimStatus status, const char* text) noexcept {
    lastStatus = static_cast<int32_t>(status);
    std::snprintf(lastError, sizeof(lastError), "%s", text == nullptr ? "" : text);
}
template<class Result, class Operation>
Result Guard(Result failure, Operation operation) noexcept {
    try {
        std::lock_guard<std::mutex> lock(coreMutex);
        Result result = operation(); Report(core.Status(), core.Error().c_str()); return result;
    } catch (const std::bad_alloc&) { Report(OniSimStatus::OutOfMemory, "Native simulation allocation failed."); }
    catch (const std::exception& e) { Report(OniSimStatus::InternalError, e.what()); }
    catch (...) { Report(OniSimStatus::InternalError, "Unknown native simulation failure."); }
    return failure;
}
}

extern "C" {
uint64_t OniSim_GetCapabilities() noexcept { return SimCore::Capabilities; }
int32_t OniSim_GetLastStatus() noexcept { return lastStatus; }
const char* OniSim_GetLastError() noexcept { return lastError; }
void OniSim_GetDiagnostics(OniSimDiagnostics* state) noexcept {
    Guard(false, [&]() { core.GetDiagnostics(state); return true; });
}
uint32_t OniSim_GetTickCount() noexcept {
    return Guard<uint32_t>(0, [&]() { return core.GetTickCount(); });
}
int32_t OniSim_CopyStabilityTimers(uint8_t* destination, int32_t length) noexcept {
    return Guard<int32_t>(0, [&]() { return core.CopyStabilityTimers(destination, length); });
}
void OniSim_Initialize(OniSim_GameMessageHandler callback) noexcept {
    Guard(false, [&]() { core.Initialize(callback); return true; });
}
void OniSim_Shutdown() noexcept { Guard(false, [&]() { core.Shutdown(); return true; }); }
void* OniSim_HandleMessage(int32_t id, int32_t bytes, const uint8_t* data) noexcept {
    return Guard<void*>(nullptr, [&]() { return core.HandleMessage(id, bytes, data); });
}
void* OniSim_HandleMessages(int32_t id, int32_t bytes, int32_t count, const uint8_t* data) noexcept {
    return Guard<void*>(nullptr, [&]() { return core.HandleMessages(id, bytes, count, data); });
}
uint8_t* OniSim_BeginSave(int32_t* bytes, int32_t x, int32_t y) noexcept {
    if (bytes != nullptr) *bytes = 0;
    return Guard<uint8_t*>(nullptr, [&]() { return core.BeginSave(bytes, x, y); });
}
void OniSim_EndSave() noexcept { Guard(false, [&]() { core.EndSave(); return true; }); }
void OniSim_DebugCrash() noexcept { Report(OniSimStatus::UnsupportedFeature, "Deliberate native crash is not supported by this core."); }
}
