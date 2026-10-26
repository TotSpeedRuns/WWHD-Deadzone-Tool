WWHD Deadzone Tool 1.2.0
Author: Tot_sr

Capture the current native controller settings and restore the selected profile
when North American/European WWHD, TWWHD Randomizer or TWWHD-GZ launches under Aroma.
For the calibration/deadzone explanation and testing scope, see Deadzone Research.txt.

Requirements
------------
Aroma Beta 26 or newer is required for this WUPS 0.9.1 build. Use the latest
Aroma release and update its modules together before installing this plugin.
Official releases: https://github.com/wiiu-env/Aroma/releases
An outdated Aroma installation was reported to black-screen at boot with this
plugin. If this happens, power off, remove WWHDDeadzone.wps from the SD plugin
folder using a PC, update Aroma, and then reinstall the plugin.

Use
---
1. Install WWHDDeadzone.wps in SD:/wiiu/environments/aroma/plugins/ and restart.
2. Launch a supported WWHD title and connect one original Wii U Pro Controller.
3. Open L + D-pad Down + Minus -> WWHD Deadzone Tool.
4. On Profile, use Left/Right to choose one of eight saved slots.
5. On Profile name, press A to edit up to 24 characters. Left/Right moves the
   cursor; Up/Down changes the character; X deletes; Y enters a space.
   Press A to save the name or B to cancel. Move past the final letter to append.
6. On Capture Deadzones, press A. The current settings are saved immediately.
7. Close the menu. The selected slot is remembered and reapplied on later launches.
8. Restore status shows readback verification or the reason replay is unavailable.

Each slot holds exactly one GamePad snapshot and one physical Pro Controller
snapshot, with separate captured settings. Empty controller entries are indicated
on the Profile row. Capturing replaces the connected controller's entry in that
slot; disconnected entries are retained. More than one connected Pro Controller
rejects Pro capture without replacing its saved entry. Only the matching physical
Pro Controller receives that slot's calibration, regardless of player channel.
Switching profiles selects the next settings to restore; it does not reset the
hardware to its launch defaults. Recapturing records the currently applied values.
Old named slots retain their names, selection, clamps and Pro captures. GamePad
entries from older versions require one recapture because they lack centers.
Unnamed legacy settings migrate into Default. Original storage
namespace wwhd_deadzone_lab is retained. Back up its JSON to preserve private
controller identities and profiles. Do not install multiple copies of this plugin.

What is captured
----------------
GamePad: four native stick-center offsets, left/right max/min clamps and cross-
clamp mode. Captured centers are locked against automatic launch recentering.
A zero-clamp disabled setting is restored using the native disable function.
Pro Controller: four native stick-center offsets, physical Bluetooth identity,
normalization thresholds and cross-clamp mode. Shared Pro thresholds/mode must
already match; restore refuses a mismatch rather than altering other controllers.
This captures existing behavior, not stick positions or an automated drift test.

Validation and limits
---------------------
GamePad and original Wii U Pro Controller restoration were checked before and
after a full Wii U restart on one console running TWWHD-GZ. Saved profiles were
unchanged, both live center readbacks matched, and each controller registered a
restore write. Both sticks/controllers were observed moving during sampling.
The GamePad check also matched clamp thresholds, origin lock and cross mode.
This verifies captured state and replay activity, not an exhaustive physical
response curve. Testing on vanilla European WWHD was also reported by Tot_sr;
the detailed telemetry evidence above was collected on TWWHD-GZ.
Native initialization must complete before replay; already-buffered reports can
retain initial values until fresh hardware input arrives.
Supported title IDs:
  North American WWHD: 0005000010143500
  TWWHD Randomizer:    0005000010143599
  European WWHD:      0005000010143600
  TWWHD-GZ:           0005000010575a01
Version 1.2.0 adds the North American game and randomizer channel to the launch
guard; the native capture/restore implementation and saved profile format are
unchanged. The selected profile is shared across supported titles, with no need
to capture again when switching games with the same controllers. Randomizer and
North American console acceptance testing is still pending; the EUR/GZ testing
above does not establish randomizer compatibility on hardware.
The randomizer channel ID is defined by upstream TWWHD Randomizer:
https://github.com/SuperDude88/TWWHD-Randomizer/blob/cb7b1c979ac031c8eae91e23fb00fc5f4d042efa/tweaks.cpp#L840
Custom repacks with a different title ID are not enabled automatically.
Game version is not independently gated. Native padscore/vpadbase accessor instructions and OS address mappings are
validated before calibration access. No game or system binaries are shipped.

Build and tests
---------------
Docker builds need only Docker on the host:
  PowerShell: ./build-docker.ps1
  Bash:       bash build-docker.sh
See BUILDING.md for outputs, local SDK builds and modifying/rebuilding WUPS.

python build.py --sdk PATH_TO_DEVKITPRO --wups PATH_TO_WUPS_SOURCE --build-dir BUILD_DIR
Dependencies used: GCC 15.2.0, newlib 4.6.0.20260123, wut 1.9.1 and WUPS 0.9.1.
WUPS commit: 0e26d9fd2d9db6c356c973a1a79000757802d883.
Compile tests/deadzone_test.cpp and tests/profiles_test.cpp with a C++20 host
compiler and assertions enabled. Tests cover native identity/signature rejection,
four-field write isolation, title filtering, signed address encoding, profile-bank
validation, v1-to-v2 migration, unlocked-but-calibrated capture, shared-code ranges,
distinct slots, storage round trips and 150050 deadzone properties.

Gecko diagnostics
-----------------
Closing the menu publishes live descriptor addresses/session in the plugin's
private config JSON. tests/gecko_check.py reads that config via FTPiiU, then only
those records with Aroma Gecko peekmultiple. Never reuse addresses from an older
session or scan random RAM. The reader encodes high addresses as negative signed
32-bit decimal to avoid the installed Gecko parser's strtol overflow.
Load a playable save and close the plugin menu in the current session first:
python tests/gecko_check.py --host WII_U_IP --phase before --seconds 15 --output-dir SCRATCH_DIR
After restarting, repeat with --phase after and the same scratch folder.
Raw captures/configs contain controller identities; do not publish them.

Licenses
--------
Plugin source: MIT, Tot_sr. WUPS retains its separate LGPL-3.0 license.
LGPL-3.0 incorporates GPL-3.0 terms; both dependency texts are included.
Readable pinned WUPS source/build files are in third-party/WiiUPluginSystem/.
Generated binaries and source ZIP archives are not tracked. See
THIRD_PARTY_NOTICES.txt for license scopes and BUILDING.md to rebuild with
modified dependencies. The release ZIP contains only the installable wiiu folder.
