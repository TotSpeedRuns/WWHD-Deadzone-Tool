#include "native_pro.hpp"
#include <atomic>
#include <cstdio>
#include <coreinit/dynload.h>
#include <coreinit/interrupts.h>
#include <coreinit/memorymap.h>
#include <padscore/wpad.h>
#include <wups/storage.h>

namespace nativepro {
static Snapshot snapshots[4];
// Menu capture and input hooks take this lock without waiting. Capture only
// copies POD state; storage IO always happens after releasing the lock.
static std::atomic_flag busy{};
static std::atomic<bool> layoutReady{false};
static uint32_t controllerTable = 0, minAddress = 0, maxAddress = 0, crossAddress = 0;
struct ChannelDebug {
    std::atomic<uint32_t> state{0}, verifiedReads{0};
    std::atomic<int32_t> before[4]{}, after[4]{};
    std::atomic<uint32_t> addressWords[2]{};
    std::atomic<uint32_t> writes{0}, profileIndex{0xffffffff};
};
struct Debug {
    std::atomic<uint32_t> magic{0x50524f31}, schema{1}, session{0}, layout{0};
    ChannelDebug channels[4];
};
static Debug debug;
static_assert(sizeof(ChannelDebug) == 56);
static_assert(sizeof(Debug) == 240);
static bool mapped(uint32_t address, uint32_t size) {
    if (address < 0x02000000 || size == 0 || address > 0xbfffffff - size) return false;
    for (uint32_t a = address & ~0xfffu; a <= (address + size - 1) && a != 0; a += 0x1000)
        if (!OSIsAddressValid(a)) return false;
    return OSIsAddressValid(address + size - 1);
}
static uint32_t word(uint32_t address) { return *reinterpret_cast<volatile uint32_t *>(address); }
static int32_t origin(uint32_t base, int index) { return *reinterpret_cast<volatile int16_t *>(base + originOffsets[index]); }
static bool signature(uint32_t address, const uint32_t *words, int count) {
    if (!mapped(address, count * 4)) return false;
    for (int i = 0; i < count; ++i) if (word(address + i * 4) != words[i]) return false;
    return true;
}
static uint32_t controller(int channel, uint8_t *identity) {
    if (!layoutReady.load() || channel < 0 || channel > 3) return 0;
    WPADExtensionType type;
    if (WPADProbe(static_cast<WPADChan>(channel), &type) != WPAD_ERROR_NONE || type != WPAD_EXT_PRO_CONTROLLER) return 0;
    WPADAddress address = {};
    WPADGetAddress(static_cast<WPADChan>(channel), &address);
    std::memcpy(identity, address.btDeviceAddress, 6);
    const uint32_t base = word(controllerTable + channel * 4);
    if ((base & 3) || !mapped(base, 0xc20)) return 0;
    // Native Pro report format 0x16 and completed origin calibration.
    if (word(base + 0x900) != 0x16 || *reinterpret_cast<volatile uint8_t *>(base + 0xb21) != 1) return 0;
    return base;
}
void load() {
    for (int i = 0; i < 4; ++i) {
        char key[32]; std::snprintf(key, sizeof(key), "pro_native_v1_%d", i);
        Snapshot s; uint32_t size = 0;
        if (WUPSStorageAPI_GetBinary(nullptr, key, &s, sizeof(s), &size) == WUPS_STORAGE_ERROR_SUCCESS && size == sizeof(s) && valid(s)) snapshots[i] = s;
    }
}
bool save() {
    Snapshot copy[4];
    if (busy.test_and_set()) return false;
    std::memcpy(copy, snapshots, sizeof(copy)); busy.clear();
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
        char key[32]; std::snprintf(key, sizeof(key), "pro_native_v1_%d", i);
        ok &= WUPSStorageAPI_StoreBinary(nullptr, key, &copy[i], sizeof(copy[i])) == WUPS_STORAGE_ERROR_SUCCESS;
    }
    return ok;
}
void start(uint32_t session) {
    layoutReady.store(false); debug.layout.store(0); debug.session.store(session);
    for (auto &c : debug.channels) { c.state.store(0); c.verifiedReads.store(0); c.writes.store(0); }
    OSDynLoad_Module module;
    if (OSDynLoad_IsModuleLoaded("padscore.rpl", &module) != OS_DYNLOAD_OK) return;
    void *function = nullptr;
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "WPADRecalibrate", &function) != OS_DYNLOAD_OK) return;
    const uint32_t recal = reinterpret_cast<uint32_t>(function);
    if (!mapped(recal, 32)) return;
    uint32_t code[8]; for (int i = 0; i < 8; ++i) code[i] = word(recal + i * 4);
    if (!recalibrateSignature(code) || recal < 0x8a14) return;
    const uint32_t text = recal - 0x8a14;
    // Confirm the Pro parser and the center-subtraction path, not just the export.
    constexpr uint32_t parser[] = {0x7c0802a6,0x9421ffe8,0x93c10010,0x7c7e1b78,0x93e10014,0x93810008,0x7c9f2378,0x93a1000c};
    constexpr uint32_t subtraction[] = {0x83bf0000,0xa8fc0888,0xa8dd0030,0x3880f800,0x7c073050,0x38a007ff,0x7c030734};
    if (!signature(text+0x16ac0, parser, 8) || !signature(text+0x16c30, subtraction, 7) || !mapped(text+0x3224, 0x264)) return;
    if (!mapped(text+0x16c00,0x88) || word(text+0x16c54)!=0xa81c088e ||
        word(text+0x16c6c)!=0xa97c0894 || word(text+0x16c84)!=0xa8dc089a ||
        word(text+0x16c00)!=0xb0dc0888 || word(text+0x16c10)!=0xb19c089a ||
        word(text+0x16c24)!=0xb0fc088e || word(text+0x16c28)!=0xb09c0894) return;
    const uint32_t table = relocatedAddress(code[4], code[6]);
    const uint32_t loMin = word(text+0x3478), hiMin = word(text+0x3470);
    const uint32_t loMax = word(text+0x3484), hiMax = word(text+0x347c);
    const uint32_t hiCross = word(text+0x3224), loCross = word(text+0x3228);
    if ((hiMin&0xffff0000)!=0x3cc00000 || (loMin&0xffff0000)!=0x80c60000 ||
        (hiMax&0xffff0000)!=0x3ce00000 || (loMax&0xffff0000)!=0x80e70000 ||
        (hiCross&0xffff0000)!=0x3d400000 || (loCross&0xffff0000)!=0x814a0000) return;
    const uint32_t mn = relocatedAddress(hiMin,loMin), mx = relocatedAddress(hiMax,loMax), cr = relocatedAddress(hiCross,loCross);
    if (table < 0x8380 || mn != table-0x5bdc || mx != table-0x5bd8 || cr != table-0x5b44 ||
        !mapped(table,16) || !mapped(mn,8) || !mapped(cr,4)) return;
    if (word(mn) != 258 || word(mx) != 919 || word(cr) > 1) return;
    controllerTable=table; minAddress=mn; maxAddress=mx; crossAddress=cr;
    debug.layout.store(1); layoutReady.store(true);
}
int capture() {
    if (!layoutReady.load() || busy.test_and_set()) return -1;
    int connected=0;
    for (int channel=0;channel<4;++channel) {
        WPADExtensionType type;
        if (WPADProbe(static_cast<WPADChan>(channel), &type)==WPAD_ERROR_NONE && type==WPAD_EXT_PRO_CONTROLLER) ++connected;
    }
    if (connected>1) { busy.clear(); return -2; }
    int count = 0;
    for (int channel = 0; channel < 4; ++channel) {
        Snapshot s; const uint32_t base = controller(channel, s.address);
        if (!base) continue;
        const BOOL irq = OSDisableInterrupts();
        s.schema=1;
        for (int i = 0; i < 4; ++i) s.origin[i] = origin(base,i);
        s.minimum=word(minAddress); s.maximum=word(maxAddress); s.cross=word(crossAddress);
        OSRestoreInterrupts(irq);
        if (!valid(s)) continue;
        for (auto &old:snapshots) old=Snapshot{};
        snapshots[0]=s; ++count;
    }
    busy.clear(); return count;
}
void restore(int channel) {
    if (channel < 0 || channel > 3 || !layoutReady.load() || busy.test_and_set()) return;
    auto &c=debug.channels[channel]; uint8_t identity[6] = {};
    const uint32_t base=controller(channel, identity);
    if (!base) { c.state.store(2); busy.clear(); return; }
    int slot=match(snapshots,identity);
    if (slot < 0) { c.state.store(1); busy.clear(); return; }
    const auto &s=snapshots[slot];
    // Thresholds/mode are global in padscore. Refuse a mismatch instead of
    // changing them for other controllers or pretending per-pad thresholds exist.
    if (word(minAddress)!=static_cast<uint32_t>(s.minimum) || word(maxAddress)!=static_cast<uint32_t>(s.maximum) || word(crossAddress)!=static_cast<uint32_t>(s.cross)) {
        c.state.store(6); busy.clear(); return;
    }
    c.profileIndex.store(slot);
    uint8_t packed[8] = {}; std::memcpy(packed, identity, 6);
    uint32_t id[2]; std::memcpy(id,packed,8);
    c.addressWords[0].store(id[0]); c.addressWords[1].store(id[1]);
    int32_t prior[4]={}, after[4]={};
    const BOOL irq=OSDisableInterrupts();
    const int changed=applyOrigins(reinterpret_cast<volatile uint8_t *>(base),s,prior,after);
    asm volatile("sync" ::: "memory");
    OSRestoreInterrupts(irq);
    if (changed<0) { c.state.store(7); busy.clear(); return; }
    for (int i=0;i<4;++i) { c.before[i].store(prior[i]); c.after[i].store(after[i]); }
    c.verifiedReads.fetch_add(1); if (changed) c.writes.fetch_add(1);
    c.state.store(changed ? 4 : 5);
    busy.clear();
}
bool supported() { return layoutReady.load(); }
bool getSnapshot(Snapshot &out) {
    if (busy.test_and_set()) return false;
    out=Snapshot{};
    for (const auto &s:snapshots) if (valid(s)) { out=s; break; }
    busy.clear(); return true;
}
bool setSnapshot(const Snapshot &s) {
    if ((s.schema && !valid(s)) || busy.test_and_set()) return false;
    for (auto &old:snapshots) old=Snapshot{};
    snapshots[0]=s;
    busy.clear(); return true;
}
int savedCount() {
    if (busy.test_and_set()) return 0;
    int count=0; for (const auto &s:snapshots) count+=valid(s);
    busy.clear(); return count;
}
uint32_t diagnosticAddress() { return reinterpret_cast<uint32_t>(&debug); }
uint32_t diagnosticSize() { return sizeof(debug); }
}
