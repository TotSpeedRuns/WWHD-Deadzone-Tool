#include "deadzone.hpp"
#include "title.hpp"
#include "native_pro.hpp"
#include "native_pad.hpp"
#include "profile_bank.hpp"
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <coreinit/title.h>
#include <coreinit/time.h>
#include <padscore/kpad.h>
#include <vpad/input.h>
#include <wups.h>
#include <wups/storage.h>
#include <wups/config_api.h>

WUPS_PLUGIN_NAME("WWHD Deadzone Tool");
WUPS_PLUGIN_DESCRIPTION("Capture and restore GamePad thresholds and per-device Pro stick origins");
WUPS_PLUGIN_VERSION("1.1.3");
WUPS_PLUGIN_AUTHOR("Tot_sr");
WUPS_PLUGIN_LICENSE("MIT");
WUPS_USE_STORAGE("wwhd_deadzone_lab");

static_assert(std::atomic<int>::is_always_lock_free);
static constexpr int CONTROLLERS = 5; // GamePad, then Pro channels 0..3.
struct Profile {
    std::atomic<bool> enabled{false};
    std::atomic<int> left{0}, right{0};
    std::atomic<bool> seen{false};
    std::atomic<unsigned> reads{0};
};
static Profile profiles[CONTROLLERS];
static std::atomic<bool> active{false}, paused{false}, replay{false};
static std::atomic<int> clamps[4]; // left max/min, right max/min
static std::atomic<bool> savedClamps{false}, fullGamepad{false};
static int captureResult = 0;
static presets::Bank bank = presets::defaults();
static bool naming = false;
static unsigned cursor = 0;
static char draft[presets::nameSize]{};
static bool applyPreset() {
    const auto &e=bank.entries[bank.selected];
    nativepro::Snapshot previous;
    if (!nativepro::getSnapshot(previous) || !nativepro::setSnapshot(e.pro)) return false;
    if (!nativepad::setSnapshot(e.gamepad)) { nativepro::setSnapshot(previous);return false; }
    savedClamps.store(false);
    for (int i=0;i<4;++i) clamps[i].store(e.clamps[i]);
    savedClamps.store(e.gamepadValid!=0);
    fullGamepad.store(e.gamepad.schema!=0);
    replay.store(e.gamepadValid!=0);
    captureResult = 0;
    return true;
}
static bool captureRequested = false;
static char captureSummary[96] = "Press A to capture connected controllers";
static char status[180] = "No native GamePad clamp snapshot saved.";
static char keys[CONTROLLERS][3][32];

// Read-only Gecko telemetry. Every field is a big-endian 32-bit word on Wii U.
// Per-channel sequence counters let the reader reject partially updated samples.
struct SampleRecord {
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> raw[4]{}, output[4]{};
    std::atomic<uint32_t> enabled{0}, left{0}, right{0}, reads{0};
};
struct Diagnostics {
    std::atomic<uint32_t> magic{0x57445a31}, schema{1}, session{0};
    std::atomic<uint32_t> titleHigh{0}, titleLow{0}, replayCalls{0};
    std::atomic<uint32_t> actualClamps[4]{};
    SampleRecord samples[CONTROLLERS];
};
static Diagnostics diagnostics;
static std::atomic_flag recordLocks[CONTROLLERS]{};
static_assert(sizeof(SampleRecord) == 13 * 4);
static_assert(sizeof(Diagnostics) == (10 + 13 * CONTROLLERS) * 4);

static void observe(int index, const float *raw, const float *output) {
    if (recordLocks[index].test_and_set()) return;
    auto &s = diagnostics.samples[index];
    s.sequence.fetch_add(1);
    for (int i = 0; i < 4; ++i) {
        s.raw[i].store(std::bit_cast<uint32_t>(raw[i]));
        s.output[i].store(std::bit_cast<uint32_t>(output[i]));
    }
    s.enabled.store(profiles[index].enabled.load());
    s.left.store(profiles[index].left.load());
    s.right.store(profiles[index].right.load());
    s.reads.store(profiles[index].reads.load());
    s.sequence.fetch_add(1);
    recordLocks[index].clear();
}

static bool validClamps(const int *v) { return presets::validClamps(v); }
static void closed() {
    const bool capture = captureRequested;
    int gamepadCaptured = 0;
    if (captureRequested && active.load() && profiles[0].seen.load()) {
        nativepad::Snapshot captured;
        if (nativepad::capture(captured)) {
            bank.entries[bank.selected].gamepad=captured;
            fullGamepad.store(true);
            for (int j=0;j<4;++j) clamps[j].store(captured.clamps[j]);
            savedClamps.store(true);replay.store(true);captureResult=1;gamepadCaptured=1;
            std::snprintf(status,sizeof(status),"Captured GamePad centers, clamps and cross mode.");
        } else {
            captureResult=-1;
            std::snprintf(status,sizeof(status),"GamePad capture unavailable: waiting for calibration or validated library layout.");
        }
    } else if (captureRequested) {
        captureResult = -1;
        std::snprintf(status, sizeof(status), "Capture requires WWHD EUR/GZ and a successful GamePad read this launch.");
    }
    if (capture && active.load()) {
        const int proCaptured = nativepro::capture();
        captureResult = gamepadCaptured || proCaptured > 0 ? 1 : -1;
        if (proCaptured >= 0) {
            std::snprintf(captureSummary, sizeof(captureSummary), "Saved: GamePad %d, Pro %d", gamepadCaptured, proCaptured);
            std::snprintf(status, sizeof(status), "Capture saved: %d GamePad, %d Pro. Pro restore matches the physical controller.", gamepadCaptured, proCaptured);
        } else if (proCaptured == -2) {
            captureResult=-1;
            std::snprintf(captureSummary,sizeof(captureSummary),"Connect only one Pro Controller; Pro unchanged");
        } else {
            std::snprintf(captureSummary, sizeof(captureSummary), "GamePad %d; Pro layout unavailable", gamepadCaptured);
            std::snprintf(status, sizeof(status), "GamePad %d captured. Pro native layout could not be validated; no Pro capture.", gamepadCaptured);
        }
    }
    captureRequested = false;
    bool ok = true;
    ok &= WUPSStorageAPI_StoreString(nullptr,"plugin_version","1.1.3")==WUPS_STORAGE_ERROR_SUCCESS;
    auto &entry=bank.entries[bank.selected];
    entry.gamepadValid=savedClamps.load();
    for (int i=0;i<4;++i) entry.clamps[i]=clamps[i].load();
    ok &= nativepro::getSnapshot(entry.pro);
    ok &= WUPSStorageAPI_StoreBinary(nullptr,"named_profiles_v2",&bank,sizeof(bank))==WUPS_STORAGE_ERROR_SUCCESS;
    for (int i = 0; i < CONTROLLERS; ++i) {
        ok &= WUPSStorageAPI_StoreBool(nullptr, keys[i][0], profiles[i].enabled.load()) == WUPS_STORAGE_ERROR_SUCCESS;
        ok &= WUPSStorageAPI_StoreInt(nullptr, keys[i][1], profiles[i].left.load()) == WUPS_STORAGE_ERROR_SUCCESS;
        ok &= WUPSStorageAPI_StoreInt(nullptr, keys[i][2], profiles[i].right.load()) == WUPS_STORAGE_ERROR_SUCCESS;
    }
    int v[4];
    for (int j = 0; j < 4; ++j) v[j] = clamps[j].load();
    ok &= WUPSStorageAPI_StoreBinary(nullptr, "gamepad_clamps_v1", v, sizeof(v)) == WUPS_STORAGE_ERROR_SUCCESS;
    ok &= WUPSStorageAPI_StoreBool(nullptr, "gamepad_clamps_valid", savedClamps.load()) == WUPS_STORAGE_ERROR_SUCCESS;
    ok &= WUPSStorageAPI_StoreBool(nullptr, "replay_clamps", replay.load()) == WUPS_STORAGE_ERROR_SUCCESS;
    // Publish the live allocation address only from the plugin itself. The host
    // must obtain a fresh descriptor after every restart, never scan random RAM.
    uint32_t descriptor[5] = {0x57445a31, 1,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&diagnostics)),
        sizeof(diagnostics), diagnostics.session.load()};
    ok &= WUPSStorageAPI_StoreBinary(nullptr, "gecko_diagnostics_v1", descriptor, sizeof(descriptor)) == WUPS_STORAGE_ERROR_SUCCESS;
    uint32_t padDescriptor[5]={0x47504431,3,nativepad::diagnosticAddress(),nativepad::diagnosticSize(),diagnostics.session.load()};
    ok &= WUPSStorageAPI_StoreBinary(nullptr,"gecko_gamepad_native_v1",padDescriptor,sizeof(padDescriptor))==WUPS_STORAGE_ERROR_SUCCESS;
    ok &= nativepro::save();
    uint32_t proDescriptor[5] = {0x50524f31,1,nativepro::diagnosticAddress(),nativepro::diagnosticSize(),diagnostics.session.load()};
    ok &= WUPSStorageAPI_StoreBinary(nullptr, "gecko_pro_native_v1", proDescriptor, sizeof(proDescriptor)) == WUPS_STORAGE_ERROR_SUCCESS;
    ok &= WUPSStorageAPI_SaveStorage(false) == WUPS_STORAGE_ERROR_SUCCESS;
    if (!ok) {
        captureResult = -1;
        std::snprintf(captureSummary, sizeof(captureSummary), "Save failed; reopen for details");
        std::snprintf(status, sizeof(status), "Storage error: current settings may not survive restart.");
    }
    paused.store(false);
}

static int32_t captureDisplay(void *, char *out, int32_t size) {
    if (size > 0) std::snprintf(out, size, "%s", captureResult ? captureSummary : "Press A to capture connected controllers");
    return 0;
}
static void captureInput(void *, WUPSConfigSimplePadData input) {
    if (!(input.buttons_d & WUPS_CONFIG_BUTTON_A)) return;
    captureRequested = true;
    closed(); // Capture, enable replay, and commit storage in one action.
    paused.store(true); // The menu remains open after the action.
}
static int32_t presetDisplay(void *, char *out, int32_t size) {
    const auto &e=bank.entries[bank.selected];
    if (size>0) std::snprintf(out,size,"< %u/8: %s >  GP:%s Pro:%s",bank.selected+1,e.name,e.gamepad.schema?"saved":e.gamepadValid?"recapture":"empty",nativepro::valid(e.pro)?"saved":"empty");
    return 0;
}
static void presetInput(void *, WUPSConfigSimplePadData input) {
    int delta=0;
    if (input.buttons_d & WUPS_CONFIG_BUTTON_LEFT) delta=-1;
    if (input.buttons_d & WUPS_CONFIG_BUTTON_RIGHT) delta=1;
    if (!delta) return;
    const unsigned old=bank.selected;
    bank.selected=(bank.selected+presets::count+delta)%presets::count;
    if (!applyPreset()) { bank.selected=old; return; }
    closed(); paused.store(true);
}
static int32_t nameDisplay(void *, char *out, int32_t size) {
    if (size<=0) return 0;
    if (!naming) std::snprintf(out,size,"%s (A: edit)",bank.entries[bank.selected].name);
    else std::snprintf(out,size,"%.*s[%c]%s",static_cast<int>(cursor),draft,draft[cursor]?draft[cursor]:'_',draft[cursor]?draft+cursor+1:"");
    return 0;
}
static bool nameMovement(void *) { return !naming; }
static void nameInput(void *, WUPSConfigSimplePadData input) {
    const auto buttons=input.buttons_d;
    if (!naming) {
        if (buttons & WUPS_CONFIG_BUTTON_A) {
            std::memcpy(draft,bank.entries[bank.selected].name,sizeof(draft));
            cursor=0; naming=true;
        }
        return;
    }
    if (buttons & WUPS_CONFIG_BUTTON_B) { naming=false; return; }
    if (buttons & WUPS_CONFIG_BUTTON_A) {
        // Trim trailing spaces; an empty edit leaves the previous name intact.
        unsigned length=std::strlen(draft);
        while (length && draft[length-1]==' ') draft[--length]=0;
        if (length) {
            auto &name=bank.entries[bank.selected].name;
            std::memset(name,0,sizeof(name));
            std::memcpy(name,draft,length);
        }
        naming=false; closed(); paused.store(true); return;
    }
    unsigned length=std::strlen(draft);
    if ((buttons & WUPS_CONFIG_BUTTON_LEFT) && cursor) --cursor;
    if ((buttons & WUPS_CONFIG_BUTTON_RIGHT) && cursor<length && cursor<presets::nameSize-2) ++cursor;
    if (buttons & WUPS_CONFIG_BUTTON_X) {
        if (cursor<length) std::memmove(draft+cursor,draft+cursor+1,length-cursor);
        return;
    }
    constexpr char alphabet[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    if (buttons & (WUPS_CONFIG_BUTTON_UP | WUPS_CONFIG_BUTTON_DOWN | WUPS_CONFIG_BUTTON_Y)) {
        const char *found=std::strchr(alphabet,draft[cursor]?draft[cursor]:' ');
        unsigned index=found ? static_cast<unsigned>(found-alphabet):0;
        const unsigned count=sizeof(alphabet)-1;
        if (buttons & WUPS_CONFIG_BUTTON_Y) index=0;
        else index=(index+count+((buttons & WUPS_CONFIG_BUTTON_UP)?1:-1))%count;
        if (cursor==length) draft[cursor+1]=0;
        draft[cursor]=alphabet[index];
    }
}
static int32_t helpDisplay(void *, char *out, int32_t size) {
    if (size>0) std::snprintf(out,size,"Name: Left/Right cursor, Up/Down letter, X delete, Y space, A save, B cancel");
    return 0;
}
static int32_t restoreDisplay(void *, char *out, int32_t size) {
    if (size<=0) return 0;
    if (!active.load()) { std::snprintf(out,size,"WWHD EUR/GZ is not running"); return 0; }
    std::snprintf(out,size,"GP: %s; Pro: %s",nativepad::restoreStatus(),nativepro::restoreStatus());
    return 0;
}
static bool addItem(WUPSConfigCategoryHandle root,const char *label,
                    int32_t (*display)(void *,char *,int32_t),
                    void (*input)(void *,WUPSConfigSimplePadData)=nullptr,
                    bool (*movement)(void *)=nullptr) {
    WUPSConfigAPIItemCallbacksV2 callbacks={};
    callbacks.getCurrentValueDisplay=display;
    callbacks.getCurrentValueSelectedDisplay=display;
    callbacks.onInput=input;
    callbacks.isMovementAllowed=movement;
    WUPSConfigAPIItemOptionsV2 options={.displayName=label,.context=nullptr,.callbacks=callbacks};
    WUPSConfigItemHandle handle;
    if (WUPSConfigAPI_Item_Create(options,&handle)!=WUPSCONFIG_API_RESULT_SUCCESS) return false;
    if (WUPSConfigAPI_Category_AddItem(root,handle)!=WUPSCONFIG_API_RESULT_SUCCESS) {
        WUPSConfigAPI_Item_Destroy(handle); return false;
    }
    return true;
}

static WUPSConfigAPICallbackStatus opened(WUPSConfigCategoryHandle root) {
    paused.store(true);
    if (active.load() && !nativepro::supported()) nativepro::start(diagnostics.session.load());
    captureResult = 0;
    naming=false;
    const bool ok = addItem(root,"Profile",presetDisplay,presetInput) &&
        addItem(root,"Profile name",nameDisplay,nameInput,nameMovement) &&
        addItem(root,"Capture Deadzones",captureDisplay,captureInput) &&
        addItem(root,"Restore status",restoreDisplay) &&
        addItem(root,"Naming controls",helpDisplay);
    if (!ok) paused.store(false);
    return ok ? WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS : WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
}

INITIALIZE_PLUGIN() {
    diagnostics.session.store(static_cast<uint32_t>(OSGetSystemTime()));
    nativepro::load();
    for (int i = 0; i < CONTROLLERS; ++i) {
        for (int j = 0; j < 3; ++j) std::snprintf(keys[i][j], sizeof(keys[i][j]), "profile_%d_%d", i, j);

    }
    int v[4] = {}; uint32_t size = 0; bool valid = false, restore = false;
    WUPSStorageAPI_GetBool(nullptr, "gamepad_clamps_valid", &valid);
    if (valid && WUPSStorageAPI_GetBinary(nullptr, "gamepad_clamps_v1", v, sizeof(v), &size) == WUPS_STORAGE_ERROR_SUCCESS && size == sizeof(v) && validClamps(v)) {
        for (int j = 0; j < 4; ++j) clamps[j].store(v[j]);
        savedClamps.store(true);
        std::snprintf(status, sizeof(status), "Loaded saved GamePad clamp thresholds.");
    }
    if (WUPSStorageAPI_GetBool(nullptr, "replay_clamps", &restore) == WUPS_STORAGE_ERROR_SUCCESS) replay.store(restore);
    presets::Bank loaded; uint32_t bankSize=0;
    if (WUPSStorageAPI_GetBinary(nullptr,"named_profiles_v2",&loaded,sizeof(loaded),&bankSize)==WUPS_STORAGE_ERROR_SUCCESS && bankSize==sizeof(loaded) && presets::valid(loaded)) {
        bank=loaded;
    } else {
        presets::LegacyBank legacy;uint32_t legacySize=0;
        const bool migrated=WUPSStorageAPI_GetBinary(nullptr,"named_profiles_v1",&legacy,sizeof(legacy),&legacySize)==WUPS_STORAGE_ERROR_SUCCESS && legacySize==sizeof(legacy) && presets::migrate(legacy,bank);
        auto &e=bank.entries[0];
        if (!migrated) {
        e.gamepadValid=savedClamps.load();
        for (int i=0;i<4;++i) e.clamps[i]=clamps[i].load();
        nativepro::getSnapshot(e.pro);
        }
    }
    applyPreset();
    WUPSConfigAPIOptionsV1 options = {.name = "WWHD Deadzone Tool"};
    WUPSConfigAPI_Init(options, opened, closed);
}
ON_APPLICATION_START() {
    active.store(false);
    diagnostics.session.store(static_cast<uint32_t>(OSGetSystemTime()));
    for (auto &p : profiles) { p.seen.store(false); p.reads.store(0); }
    paused.store(false);
    const uint64_t title = OSGetTitleID();
    diagnostics.titleHigh.store(static_cast<uint32_t>(title >> 32));
    diagnostics.titleLow.store(static_cast<uint32_t>(title));
    diagnostics.replayCalls.store(0);
    for (auto &s : diagnostics.samples) { s.reads.store(0); s.sequence.store(0); }
    if (supportedTitle(title)) {
        nativepro::start(diagnostics.session.load());
        nativepad::start(diagnostics.session.load());
    }
    active.store(supportedTitle(title));
}
ON_APPLICATION_ENDS() { active.store(false); }

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *data, uint32_t capacity, VPADReadError *error) {
    const bool work = active.load() && !paused.load() && chan == VPAD_CHAN_0 && data && capacity;
    // Wait for at least one successful read after init before using VPAD setters.
    if (work && profiles[0].seen.load() && replay.load() && savedClamps.load()) {
        if (fullGamepad.load()) {
            nativepad::restore();
        } else {
            // Legacy profiles have no recoverable centers. Keep their thresholds
            // until the user explicitly captures a complete GamePad snapshot.
            VPADSetLStickClampThreshold(chan,clamps[0].load(),clamps[1].load());
            VPADSetRStickClampThreshold(chan,clamps[2].load(),clamps[3].load());
            if (!clamps[1].load()) VPADDisableLStickZeroClamp(chan);
            if (!clamps[3].load()) VPADDisableRStickZeroClamp(chan);
            nativepad::restore(); // Publishes that centers still need capture.
        }
        int v[4] = {};
        VPADGetLStickClampThreshold(chan, &v[0], &v[1]);
        VPADGetRStickClampThreshold(chan, &v[2], &v[3]);
        for (int j = 0; j < 4; ++j) diagnostics.actualClamps[j].store(v[j]);
        diagnostics.replayCalls.fetch_add(1);
    }
    int32_t result = real_VPADRead(chan, data, capacity, error);
    if (!work || result <= 0 || (error && *error != VPAD_READ_SUCCESS)) return result;
    profiles[0].seen.store(true);
    profiles[0].reads.fetch_add(1);
    const uint32_t count = static_cast<uint32_t>(result) < capacity ? result : capacity;
    for (uint32_t i = 0; i < count; ++i) {
        const float raw[4] = {data[i].leftStick.x, data[i].leftStick.y, data[i].rightStick.x, data[i].rightStick.y};
        if (profiles[0].enabled.load()) {
            deadzone::apply(data[i].leftStick.x, data[i].leftStick.y, profiles[0].left.load());
            deadzone::apply(data[i].rightStick.x, data[i].rightStick.y, profiles[0].right.load());
        }
        if (i == 0) {
            const float output[4] = {data[i].leftStick.x, data[i].leftStick.y, data[i].rightStick.x, data[i].rightStick.y};
            observe(0, raw, output);
        }
    }
    return result;
}

DECL_FUNCTION(uint32_t, KPADReadEx, KPADChan chan, KPADStatus *data, uint32_t capacity, KPADError *error) {
    const int channel = static_cast<int>(chan);
    if (active.load() && !paused.load() && data && capacity && channel >= 0 && channel <= 3) nativepro::restore(channel);
    const uint32_t result = real_KPADReadEx(chan, data, capacity, error);
    if (!active.load() || paused.load() || !data || channel < 0 || channel > 3 || (error && *error != KPAD_ERROR_OK)) return result;
    if (result) nativepro::ensureReady();
    auto &p = profiles[channel + 1];
    const uint32_t count = result < capacity ? result : capacity;
    for (uint32_t i = 0; i < count; ++i) {
        if (data[i].extensionType != WPAD_EXT_PRO_CONTROLLER || data[i].error != 0) continue;
        p.seen.store(true); p.reads.fetch_add(1);
        const float raw[4] = {data[i].pro.leftStick.x, data[i].pro.leftStick.y, data[i].pro.rightStick.x, data[i].pro.rightStick.y};
        if (p.enabled.load()) {
            deadzone::apply(data[i].pro.leftStick.x, data[i].pro.leftStick.y, p.left.load());
            deadzone::apply(data[i].pro.rightStick.x, data[i].pro.rightStick.y, p.right.load());
        }
        if (i == 0) {
            const float output[4] = {data[i].pro.leftStick.x, data[i].pro.leftStick.y, data[i].pro.rightStick.x, data[i].pro.rightStick.y};
            observe(channel + 1, raw, output);
        }
    }
    return result;
}
WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);
WUPS_MUST_REPLACE(KPADReadEx, WUPS_LOADER_LIBRARY_PADSCORE, KPADReadEx);
