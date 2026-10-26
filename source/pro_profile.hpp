#pragma once
#include <cstdint>
#include <cstring>
namespace nativepro {
struct Snapshot {
    uint32_t schema = 0;
    int32_t origin[4]{};
    int32_t minimum = 0, maximum = 0, cross = 0;
    uint8_t address[6]{};
    uint8_t reserved[2]{};
};
static_assert(sizeof(Snapshot) == 40);
inline constexpr uint32_t originOffsets[4] = {0x888,0x88e,0x894,0x89a};
inline bool valid(const Snapshot &s) {
    if (s.schema != 1 || s.minimum < 0 || s.maximum <= s.minimum || s.maximum > 2048 || (s.cross != 0 && s.cross != 1)) return false;
    bool nonzero = false;
    for (auto b : s.address) nonzero |= b != 0;
    if (!nonzero) return false;
    for (auto value : s.origin) if (value < -2048 || value > 2047) return false;
    return true;
}
inline int match(const Snapshot *profiles, const uint8_t *address) {
    for (int i = 0; i < 4; ++i)
        if (valid(profiles[i]) && std::memcmp(profiles[i].address, address, 6) == 0) return i;
    return -1;
}
// Caller validates the native object and handles synchronization. Only the four
// identified center halfwords are ever changed; all neighboring state stays intact.
inline int applyOrigins(volatile uint8_t *base, const Snapshot &s, int32_t *before, int32_t *after) {
    if (!valid(s)) return -1;
    for (int i=0; i<4; ++i) {
        before[i]=*reinterpret_cast<volatile int16_t *>(base+originOffsets[i]);
        if (before[i]<-2048 || before[i]>2047) return -1;
    }
    bool changed=false;
    for (int i=0; i<4; ++i) {
        auto *field=reinterpret_cast<volatile int16_t *>(base+originOffsets[i]);
        if (before[i]!=s.origin[i]) { *field=static_cast<int16_t>(s.origin[i]); changed=true; }
        after[i]=*field;
    }
    return changed ? 1 : 0;
}
// PPC relocation uses signed low-half immediates, including low >= 0x8000.
inline uint32_t relocatedAddress(uint32_t high, uint32_t low) {
    return ((high & 0xffffu) << 16) + static_cast<int16_t>(low & 0xffffu);
}
inline bool recalibrateSignature(const uint32_t *code) {
    return code[0] == 0x7c0802a6 && code[1] == 0x9421fff0 &&
        code[2] == 0x93e1000c && code[3] == 0x90010014 &&
        (code[4] & 0xffff0000) == 0x3c000000 && code[5] == 0x546c103a &&
        (code[6] & 0xffff0000) == 0x30000000 && code[7] == 0x7fec002e;
}
}
