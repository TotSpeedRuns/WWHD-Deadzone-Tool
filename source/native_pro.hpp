#pragma once
#include "pro_profile.hpp"
#include <cstdint>
namespace nativepro {
void load();
bool save();
void start(uint32_t session);
int capture();
void restore(int channel);
bool supported();
int savedCount();
bool getSnapshot(Snapshot &out);
bool setSnapshot(const Snapshot &snapshot);
uint32_t diagnosticAddress();
uint32_t diagnosticSize();
}
