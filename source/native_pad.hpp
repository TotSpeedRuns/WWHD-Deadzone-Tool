#pragma once
#include "gamepad_profile.hpp"
namespace nativepad {
void start(uint32_t session);
bool setSnapshot(const Snapshot &snapshot);
bool capture(Snapshot &snapshot);
void restore();
const char *restoreStatus();
uint32_t diagnosticAddress();
uint32_t diagnosticSize();
}
