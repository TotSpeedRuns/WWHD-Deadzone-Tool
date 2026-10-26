#pragma once
#include <cstdint>
constexpr bool supportedTitle(uint64_t id) {
    return id == 0x0005000010143600ULL || id == 0x0005000010575a01ULL;
}
