#include "../source/profile_bank.hpp"
#include <cassert>
#include <cstring>
int main() {
    static_assert(nativepad::validRange(0x01000000,124));
    static_assert(nativepad::validRange(0x01800f9c,124));
    static_assert(!nativepad::validRange(0,124));
    static_assert(!nativepad::validRange(0x00ffffff,124));
    static_assert(!nativepad::validRange(0xbfffffff,124));
    static_assert(!nativepad::validRange(0x01000000,0));
    static_assert(!nativepad::validRange(0xfffffff0,0x100));
    auto bank=presets::defaults();
    assert(presets::valid(bank));
    auto &first=bank.entries[0];
    first.gamepadValid=1;
    const int32_t clamps[4]={919,258,919,258};
    std::memcpy(first.clamps,clamps,sizeof(clamps));
    first.pro.schema=1; first.pro.minimum=258; first.pro.maximum=919;
    first.pro.address[0]=1; first.pro.origin[0]=37;
    assert(presets::valid(bank));
    auto copy=bank;
    copy.selected=1;
    std::snprintf(copy.entries[1].name,presets::nameSize,"Second controller");
    copy.entries[1].pro=first.pro;
    copy.entries[1].pro.address[0]=2;
    copy.entries[1].pro.origin[0]=-40;
    assert(presets::valid(copy));
    assert(copy.entries[0].pro.origin[0]==37);
    assert(copy.entries[0].gamepadValid==1 && copy.entries[1].gamepadValid==0);
    // Byte round trip models the fixed-layout storage API on the same console.
    unsigned char bytes[sizeof(copy)]; std::memcpy(bytes,&copy,sizeof(copy));
    presets::Bank restored; std::memcpy(&restored,bytes,sizeof(restored));
    assert(presets::valid(restored) && restored.selected==1);
    assert(std::strcmp(restored.entries[1].name,"Second controller")==0);
    restored.selected=8; assert(!presets::valid(restored)); restored=copy;
    restored.entries[0].name[24]='x'; assert(!presets::valid(restored)); restored=copy;
    restored.entries[0].clamps[1]=919; assert(!presets::valid(restored)); restored=copy;
    restored.entries[1].pro.origin[0]=2048; assert(!presets::valid(restored)); restored=copy;
    restored.entries[1].pro.schema=2; assert(!presets::valid(restored));
    nativepad::Snapshot pad;
    pad.schema=1;pad.locked=1;pad.cross=1;
    for (int i=0;i<4;++i) { pad.clamps[i]=clamps[i];pad.centers[i]=static_cast<int16_t>(2048+i); }
    assert(nativepad::valid(pad));
    auto unlocked=pad;unlocked.locked=0;
    assert(nativepad::captureSourceValid(unlocked,1));
    assert(!nativepad::captureSourceValid(unlocked,0));
    assert(nativepad::captureSourceValid(pad,0));
    assert(!nativepad::captureSourceValid(pad,2));
    unlocked.locked=2;assert(!nativepad::captureSourceValid(unlocked,1));
    auto changed=pad;changed.centers[0]++;
    assert(!nativepad::equal(changed,pad));
    changed=pad;changed.cross=0;assert(!nativepad::equal(changed,pad));
    changed=pad;changed.clamps[1]=0;assert(nativepad::valid(changed));
    changed=pad;changed.locked=0;assert(!nativepad::valid(changed));
    changed=pad;changed.centers[3]=4096;assert(!nativepad::valid(changed));
    copy.entries[0].gamepad=pad;assert(presets::valid(copy));
    copy.entries[0].gamepad.clamps[0]--;assert(!presets::valid(copy));
    // Every legacy name, selection, clamp and Pro identity survives migration;
    // unavailable GamePad centers must remain explicitly absent.
    presets::LegacyBank legacy;legacy.selected=7;
    for (unsigned i=0;i<presets::count;++i) {
        auto &entry=legacy.entries[i];std::snprintf(entry.name,presets::nameSize,"Old %u",i);
        entry.gamepadValid=1;std::memcpy(entry.clamps,clamps,sizeof(clamps));
        entry.pro=first.pro;entry.pro.address[0]=static_cast<uint8_t>(i+1);
    }
    presets::Bank migrated;assert(presets::migrate(legacy,migrated));
    assert(migrated.schema==2 && migrated.selected==7);
    for (unsigned i=0;i<presets::count;++i) {
        assert(std::strcmp(migrated.entries[i].name,legacy.entries[i].name)==0);
        assert(migrated.entries[i].pro.address[0]==i+1);
        assert(migrated.entries[i].gamepad.schema==0);
        assert(migrated.entries[i].gamepadValid==1);
    }
    legacy.entries[3].pro.origin[0]=4096;
    assert(!presets::migrate(legacy,migrated));
}
