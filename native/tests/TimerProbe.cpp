#include "StabilityTimers.h"

#include <cstdint>
#include <fstream>
#include <iostream>

namespace {
bool ReadU32(std::istream& in, uint32_t& value) {
    unsigned char bytes[4];
    if (!in.read(reinterpret_cast<char*>(bytes), 4)) return false;
    value = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
        (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
    return true;
}
void WriteU32(std::ostream& out, uint32_t value) {
    const unsigned char bytes[] = {static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24)};
    out.write(reinterpret_cast<const char*>(bytes), 4);
}
}

// Diagnostic only: no expected-value formula is present. Each input record is
// initial timer byte, seed uint32 LE, call count uint32 LE. Every real module
// invocation writes return byte, timer byte, seed uint32 LE.
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    std::ofstream output(argv[2], std::ios::binary);
    if (!input || !output) return 3;
    while (input.peek() != std::char_traits<char>::eof()) {
        const int first = input.get();
        uint8_t timer = static_cast<uint8_t>(first);
        uint32_t seed = 0, calls = 0;
        if (!ReadU32(input, seed) || !ReadU32(input, calls) || calls > 1000000) return 4;
        for (uint32_t i = 0; i < calls; ++i) {
            const uint8_t result = AdvanceStableCellTimerV744825(timer, seed);
            output.put(static_cast<char>(result));
            output.put(static_cast<char>(timer));
            WriteU32(output, seed);
        }
    }
    if (input.bad() || !output) return 5;
    return 0;
}
