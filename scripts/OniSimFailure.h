#pragma once

#include <cstring>

// Used only around simulator initialization and error-cleanup boundaries.
// PlatformNotSupportedException also covers the linked core's capability gate.
// Never substitute simulated data or swallow unrelated managed exceptions.
inline bool OniPortMissingSimLibrary(const Il2CppExceptionWrapper& error) {
    if (error.ex == nullptr) return false;
    for (auto klass = error.ex->klass; klass != nullptr; klass = klass->parent) {
        if (klass->namespaze != nullptr && std::strcmp(klass->namespaze, "System") == 0 &&
            klass->name != nullptr &&
            (std::strcmp(klass->name, "DllNotFoundException") == 0 ||
             std::strcmp(klass->name, "EntryPointNotFoundException") == 0 ||
             std::strcmp(klass->name, "PlatformNotSupportedException") == 0)) return true;
    }
    return false;
}

template<typename Initialize, typename ReportFailure>
bool OniPortInitializeSim(Initialize initialize, ReportFailure reportFailure) {
    try {
        initialize();
        return true;
    } catch (const Il2CppExceptionWrapper& error) {
        if (!OniPortMissingSimLibrary(error)) throw;
        reportFailure(error.ex);
        return false;
    }
}

template<typename Shutdown>
void OniPortShutdownAfterWorldgenFailure(Shutdown shutdown) {
    try {
        shutdown();
    } catch (const Il2CppExceptionWrapper& error) {
        if (!OniPortMissingSimLibrary(error)) throw;
    }
}
