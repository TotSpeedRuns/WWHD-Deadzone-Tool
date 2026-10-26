#pragma once
#include <cstdint>
namespace nativepad {
// Shared Cafe system code can be below the game's 0x02000000 text region.
// The caller must additionally validate OS mappings before dereferencing.
inline constexpr bool validRange(uint32_t p,uint32_t size) {
    return p>=0x01000000u && size && size<=0xbfffffffu && p<=0xbfffffffu-size;
}
struct Snapshot {
    uint32_t schema=0;
    int32_t clamps[4]{};
    int16_t centers[4]{};
    uint32_t locked=0, cross=0;
};
static_assert(sizeof(Snapshot)==36);
inline bool valid(const Snapshot &s) {
    if (s.schema!=1 || s.locked!=1 || s.cross>1) return false;
    for (int i=0;i<4;++i) if (s.centers[i]<0 || s.centers[i]>4095) return false;
    return s.clamps[0]>0 && s.clamps[0]<=919 && s.clamps[1]>=0 && s.clamps[1]<s.clamps[0] &&
           s.clamps[2]>0 && s.clamps[2]<=919 && s.clamps[3]>=0 && s.clamps[3]<s.clamps[2];
}
inline bool equal(const Snapshot &a,const Snapshot &b) {
    if (!valid(a) || !valid(b) || a.cross!=b.cross) return false;
    for (int i=0;i<4;++i) if (a.clamps[i]!=b.clamps[i] || a.centers[i]!=b.centers[i]) return false;
    return true;
}
inline bool captureSourceValid(const Snapshot &source,uint32_t originReady) {
    Snapshot frozen=source;frozen.locked=1;
    return source.locked<=1 && originReady<=1 && (source.locked || originReady) && valid(frozen);
}
}
