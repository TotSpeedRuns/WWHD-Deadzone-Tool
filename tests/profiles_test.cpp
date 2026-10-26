#include "../source/profile_bank.hpp"
#include <cassert>
#include <cstring>
int main() {
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
}
