#pragma once
#include <cstdint>
constexpr bool supportedTitle(uint64_t id) {
    // The USA randomizer installs a separate channel; do not match by prefix.
    return id == 0x0005000010143500ULL || // North American WWHD
           id == 0x0005000010143599ULL || // TWWHD Randomizer
           id == 0x0005000010143600ULL || // European WWHD
           id == 0x0005000010575a01ULL;   // TWWHD-GZ
}
