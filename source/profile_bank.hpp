#pragma once
#include "pro_profile.hpp"
#include <cstdio>
namespace presets {
constexpr unsigned count = 8, nameSize = 25;
inline bool validClamps(const int32_t *v) {
    return v[0]>0 && v[0]<=32767 && v[1]>=0 && v[1]<v[0] &&
           v[2]>0 && v[2]<=32767 && v[3]>=0 && v[3]<v[2];
}
struct Entry {
    char name[nameSize]{};
    uint8_t padding[3]{};
    uint32_t gamepadValid = 0;
    int32_t clamps[4]{};
    nativepro::Snapshot pro{}; // Exactly one physical Pro Controller per preset.
};
struct Bank {
    uint32_t schema = 1, selected = 0;
    Entry entries[count]{};
};
inline Bank defaults() {
    Bank bank;
    for (unsigned i=0;i<count;++i)
        std::snprintf(bank.entries[i].name,nameSize,i ? "Profile %u" : "Default",i+1);
    return bank;
}
inline bool valid(const Bank &bank) {
    if (bank.schema!=1 || bank.selected>=count) return false;
    for (const auto &e:bank.entries) {
        if (!e.name[0] || e.name[nameSize-1]!=0 || e.gamepadValid>1) return false;
        bool ended=false;
        for (char c:e.name) {
            if (!c) ended=true;
            else if (ended || c<32 || c>126) return false;
        }
        if (e.gamepadValid && !validClamps(e.clamps)) return false;
        if (e.pro.schema && !nativepro::valid(e.pro)) return false;
    }
    return true;
}
}
