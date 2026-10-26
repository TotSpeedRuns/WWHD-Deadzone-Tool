#include "native_pad.hpp"
#include "pro_profile.hpp"
#include <atomic>
#include <coreinit/dynload.h>
#include <coreinit/memorymap.h>
#include <vpad/input.h>

namespace nativepad {
using GetOffsets=void (*)(VPADChan,int16_t *,uint32_t *);
using SetOffsets=void (*)(VPADChan,const int16_t *,uint32_t);
static GetOffsets getOffsets=nullptr;
static SetOffsets setOffsets=nullptr;
static uint32_t crossAddress=0,originReadyAddress=0;
static Snapshot saved;
static std::atomic_flag busy{};
static std::atomic<bool> ready{false};
static std::atomic<unsigned> attempts{0};
struct Debug {
    std::atomic<uint32_t> magic{0x47504431},schema{3},session{0},layout{0},state{0},writes{0},reads{0},reserved{0};
    std::atomic<int32_t> centers[4]{},clamps[4]{};
    std::atomic<uint32_t> locked{0},cross{0},failedIndex{0},actualWord{0},expectedWord{0},wordMask{0},originReady{0};
};
static Debug debug;
static_assert(sizeof(Debug)==92);
static bool mapped(uint32_t p,uint32_t size) {
    if (!validRange(p,size)) return false;
    for (uint32_t a=p&~0xfffu;a<=p+size-1;a+=0x1000) if (!OSIsAddressValid(a)) return false;
    return OSIsAddressValid(p+size-1);
}
static uint32_t word(uint32_t p) { return *reinterpret_cast<volatile uint32_t *>(p); }
static void *exported(OSDynLoad_Module module,const char *name) {
    void *ptr=nullptr;
    return OSDynLoad_FindExport(module,OS_DYNLOAD_EXPORT_FUNC,name,&ptr)==OS_DYNLOAD_OK ? ptr : nullptr;
}
// Match the complete known accessor bodies. Only relocated data immediates and
// linked branch displacements may vary. Calls use the library's own IRQ guards.
static bool accessor(uint32_t p,const uint32_t *pattern,unsigned count) {
    if (!mapped(p,count*4)) {
        debug.failedIndex.store(0xffffffff);debug.actualWord.store(p);
        debug.expectedWord.store(0x01000000);debug.wordMask.store(0);return false;
    }
    for (unsigned i=0;i<count;++i) {
        const uint32_t actual=word(p+4*i), expected=pattern[i];
        const uint32_t mask=(i==9 || i==10) ? 0xffff0000u : (expected&0xfc000003u)==0x48000001u ? 0xfc000003u : 0xffffffffu;
        if ((actual&mask)!=(expected&mask)) {
            debug.failedIndex.store(i);debug.actualWord.store(actual);debug.expectedWord.store(expected);debug.wordMask.store(mask);return false;
        }
    }
    return true;
}
static void resolve() {
    OSDynLoad_Module base,vpad;
    debug.reserved.store(10);
    if (OSDynLoad_IsModuleLoaded("vpadbase.rpl",&base)!=OS_DYNLOAD_OK) return;
    debug.reserved.store(11);
    if (OSDynLoad_IsModuleLoaded("vpad.rpl",&vpad)!=OS_DYNLOAD_OK) return;
    debug.reserved.store(12);
    auto *get=exported(base,"VPADBASEGetStickOffset"), *set=exported(base,"VPADBASESetStickOffset");
    auto *clamp=exported(vpad,"VPADGetLStickClampThreshold");
    constexpr uint32_t gp[]={0x7c0802a6,0x9421ffe8,0x93e10014,0x7c9f2378,0x93a1000c,0x93c10010,0x9001001c,0x7cbd2b78,0x1c030380,0x3d001000,0x39082740,0x7fc80214,0x48004ad5,0xa93e0218,0xb13f0000,0xa95e021a,0xb15f0002,0xa97e021c,0x801e027c,0xb17f0004,0xa99e021e,0x901d0000,0xb19f0006,0x48005141,0x83a1000c,0x8001001c,0x83c10010,0x7c0803a6,0x83e10014,0x38210018,0x4e800020};
    constexpr uint32_t sp[]={0x7c0802a6,0x9421ffe8,0x93e10014,0x7c9f2378,0x93a1000c,0x93c10010,0x9001001c,0x7cbd2b78,0x1c030380,0x3d201000,0x39292740,0x7fc90214,0x48004a59,0xa95f0000,0xa97f0002,0xb15e0218,0xa99f0004,0xb17e021a,0xb19e021c,0xa81f0006,0x93be027c,0xb01e021e,0x480050c9,0x83a1000c,0x8001001c,0x83c10010,0x7c0803a6,0x83e10014,0x38210018,0x4e800020};
    const uint32_t ga=reinterpret_cast<uint32_t>(get),sa=reinterpret_cast<uint32_t>(set),ca=reinterpret_cast<uint32_t>(clamp);
    if (!get || !set || !clamp) return;
    debug.reserved.store(13);if (!accessor(ga,gp,31)) return;
    debug.reserved.store(14);if (!accessor(sa,sp,30)) return;
    debug.reserved.store(15);if (!mapped(ca,36)) return;
    debug.reserved.store(16);
    const uint32_t data=nativepro::relocatedAddress(word(ga+36),word(ga+40));
    if (!mapped(data,0x380) || data!=nativepro::relocatedAddress(word(sa+36),word(sa+40))) return;
    debug.reserved.store(17);
    constexpr uint32_t cp[]={0x1c032308,0x3d801000,0x398c0d68,0x7d4c0214,0x816a0bfc,0x91650000,0x818a0c00,0x91840000,0x4e800020};
    for (unsigned i=0;i<9;++i) {
        const auto mask=(i==1 || i==2) ? 0xffff0000u:0xffffffffu;
        const uint32_t actual=word(ca+4*i);
        if ((actual&mask)!=(cp[i]&mask)) {
            debug.failedIndex.store(i);debug.actualWord.store(actual);debug.expectedWord.store(cp[i]);debug.wordMask.store(mask);return;
        }
    }
    debug.reserved.store(18);
    const uint32_t addr=nativepro::relocatedAddress(word(ca+4),word(ca+8))+0xc04;
    if (!mapped(addr,4) || word(addr)>1) return;
    getOffsets=reinterpret_cast<GetOffsets>(get); setOffsets=reinterpret_cast<SetOffsets>(set); crossAddress=addr;originReadyAddress=data+0x278;
    ready.store(true); debug.layout.store(1);debug.reserved.store(100);
}
void start(uint32_t session) {
    if (busy.test_and_set()) return;
    ready.store(false);debug.layout.store(0);debug.session.store(session);debug.state.store(0);debug.writes.store(0);debug.reads.store(0);attempts.store(0);
    resolve();busy.clear();
}
static bool read(Snapshot &out) {
    if (!ready.load()) return false;
    out=Snapshot{};out.schema=1;
    getOffsets(VPAD_CHAN_0,out.centers,&out.locked);
    VPADGetLStickClampThreshold(VPAD_CHAN_0,&out.clamps[0],&out.clamps[1]);
    VPADGetRStickClampThreshold(VPAD_CHAN_0,&out.clamps[2],&out.clamps[3]);
    out.cross=word(crossAddress);
    const uint32_t originReady=*reinterpret_cast<volatile uint8_t *>(originReadyAddress);
    debug.originReady.store(originReady);
    for (int i=0;i<4;++i) { debug.centers[i].store(out.centers[i]);debug.clamps[i].store(out.clamps[i]); }
    debug.locked.store(out.locked);debug.cross.store(out.cross);
    // 0x27c suppresses automatic origin capture; 0x278 marks completed native
    // calibration. A zero lock with a ready origin is a valid capture source.
    return captureSourceValid(out,originReady);
}
bool capture(Snapshot &out) {
    if (busy.test_and_set()) return false;
    if (!ready.load()) resolve();
    Snapshot value;const bool ok=read(value);
    if (ok) {
        // Lock the captured centers so a later native initialization cannot
        // replace them. This does not recenter or sample the current stick pose.
        value.locked=1;saved=value;out=value;
    }
    busy.clear();return ok;
}
bool setSnapshot(const Snapshot &snapshot) {
    if ((snapshot.schema && !valid(snapshot)) || busy.test_and_set()) return false;
    saved=snapshot;busy.clear();return true;
}
void restore() {
    if (busy.test_and_set()) return;
    if (!valid(saved)) {
        if (ready.load()) { Snapshot observed;read(observed); }
        debug.state.store(1);busy.clear();return;
    }
    if (!ready.load() && attempts.fetch_add(1)%120==0) resolve();
    if (!ready.load()) { debug.state.store(3);busy.clear();return; }
    Snapshot actual;
    if (!read(actual)) { debug.state.store(2);busy.clear();return; }
    const bool changed=!equal(saved,actual);
    if (changed) {
        setOffsets(VPAD_CHAN_0,saved.centers,saved.locked);
        VPADSetLStickClampThreshold(VPAD_CHAN_0,saved.clamps[0],saved.clamps[1]);
        VPADSetRStickClampThreshold(VPAD_CHAN_0,saved.clamps[2],saved.clamps[3]);
        if (!saved.clamps[1]) VPADDisableLStickZeroClamp(VPAD_CHAN_0);
        if (!saved.clamps[3]) VPADDisableRStickZeroClamp(VPAD_CHAN_0);
        if (saved.cross) VPADEnableStickCrossClamp(VPAD_CHAN_0); else VPADDisableStickCrossClamp(VPAD_CHAN_0);
        debug.writes.fetch_add(1);
    }
    const bool verified=read(actual) && equal(saved,actual);
    for (int i=0;i<4;++i) { debug.centers[i].store(actual.centers[i]);debug.clamps[i].store(actual.clamps[i]); }
    debug.locked.store(actual.locked);debug.cross.store(actual.cross);debug.reads.fetch_add(1);
    debug.state.store(verified ? changed ? 4:5 : 6);
    busy.clear();
}
const char *restoreStatus() {
    switch (debug.state.load()) {
    case 1:return "centers not captured; recapture needed";
    case 2:return "waiting for calibration";
    case 3:return "layout unavailable";
    case 4:case 5:return "centers and clamps verified";
    case 6:return "readback mismatch";
    default:return "awaiting game input";
    }
}
uint32_t diagnosticAddress() { return reinterpret_cast<uint32_t>(&debug); }
uint32_t diagnosticSize() { return sizeof(debug); }
}
