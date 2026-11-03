# Validation inputs and tools

`inputs.json` pins the four externally supplied inputs by URL and SHA-256.
Downloads, ROMs, extracted proprietary files and emulator state stay outside Git.
Python 3.11 or newer is required for the host tools.

```sh
python3 tools/fetch_inputs.py downloads
NGAGE_VALIDATION_INPUTS="$PWD/downloads" python3 -m unittest discover -s tests -v
python3 tools/extract_client.py downloads/n97-ngage.sisx \
  --output build/original-client.sisx
```

The extractor reads the original installer controller to locate the embedded
client, checks package CRCs, and publishes the original client bytes. Repacking
checks retain the original signed controller and payload data. These host checks
cover package integrity; runtime validation is performed separately in EKA2L1.

Start a locally built desktop EKA2L1 with `--debug-control-port 8765`. Run
`tools/control.py` in the same network namespace as the emulator, for example:

```sh
python3 tools/control.py GET /v1/status --output artifacts/status.json
python3 tools/control.py GET /v1/screenshot --output artifacts/screen.png
```

The required runtime acceptance sequence is:

1. Install the original N97 installer on an isolated N97 ROM profile.
2. Place the unchanged `.n-gage` file in `E:\n-gage`.
3. Open Games, complete game installation there, and launch the installed game.
4. Exercise gameplay and retain screenshots, input history and emulator logs.
5. Repeat on an isolated X7 profile with the adapted unsigned installer.
6. Verify left directional controls, right A/B controls, hide/show, held keys,
   key release on hiding or app switching, and translucent rendering.

Emulator reference revision: `8ad3e14dad84db064c6efe29abf3553e26610a67`.
Native application SDK: `hpnkv/symbian-platform` release `v0.3.0`.

## GitHub Actions input caching

The reusable `.github/actions/cache-input` action restores an individual file by
its expected SHA-256, then runs the same hash verification as local builds. A
cache hit makes no request to the original server. There are no broad fallback
keys that could substitute a different version. GitHub may evict inactive caches;
a missing cache is populated from the pinned source again.

The package tooling workflow requests `n97-ngage.sisx` and the small pinned
Belle service from `hacked-x7.json`. ROMs and the sample
game are reserved for emulator validation jobs. Updating one resource's digest
does not invalidate cached copies of the other resources. Local downloads are
also reused and verified; `--only FILENAME` selects a single manifest entry.

The native SDK has a separate `validation/sdk.json` manifest. Build jobs can use
the same cache action with `manifest: validation/sdk.json` and
`filename: symbian-sdk-0.3.0-linux-x86_64.tar.gz`. Runtime test jobs select ROMs
and the game individually from `validation/inputs.json`.

Cache behavior was exercised in Actions run `41217431339`: attempt 1 downloaded
and saved the pinned installer; attempt 2 restored the same key and verified the
local file without downloading. All four package checks passed on both attempts.

## Runtime results (2026-11-01)

N97 baseline completed with the original installer: unchanged `.n-gage` installed
inside Games, Trial launched, English/name selection completed, Instant Play
entered the boat map and fishing screen. Further fishing was excluded from
acceptance by the user.

X7/Belle runs the adapted N97 profile and the native PostingSurfaceFactory DLL.
The guest DLL uses the ROM's existing window/bitmap APIs, with no emulator-only
imports. Touch OK and directional navigation reach the Costa Rica boat map;
translucent controls hide and reappear while gameplay keeps updating. No physical
phone has been tested. Other games, earlier Symbian^3 releases, rotation/mirror
posting formats and network services have not been validated.

The supplied X7 ROM's `uiss.dll` requests `YnstallServer`, but its C-drive service
is missing. `hacked-x7.json` pins a public patched Belle server; the build adapts
its `GetSisFileNameL` error handling for supported filename-less MSisDataProvider
streams and includes it in the unsigned SIS. It replaces
`C:\sys\bin\YnstallServer.exe`. This service's certificate behavior comes
from the public hacked binary; this project does not supply game licenses or
modify the sample game's bytes. The original ROM archives remain unchanged.

The service's decompression is checked against an independent C++ decoder and
its adaptation preserves original instructions apart from one call and an added
16-byte error-handler trampoline. Fourteen host package/service checks pass. With the included formal service,
Games completes installation of the unchanged game (Installation complete).

A repeat-install fixture initially returned `KErrAlreadyExists` after emulator
host uninstall left C:\sys\hash entries for COTD.exe, BiNPDAlibcCOTD.dll and
ngifilemanagerfactory_2000afbc.dll behind. These three stale hashes were moved out of the isolated profile before repeating the
inside-Games installation. This cleanup is test setup, not part of the SIS.

EKA2L1 fixes are in [fix/desktop-first-device-boot](https://github.com/yeatse/EKA2L1/tree/fix/desktop-first-device-boot),
branched from `8ad3e14dad84db064c6efe29abf3553e26610a67`. They cover first-device
boot, installer callbacks, sparse-container iteration, ROM GOOM exports,
animation/property teardown, patch UID identity, invalid-GC guest panics,
CDirectScreenBitmap region semantics, and ScreenPlay alpha-window composition.
The latter includes a guest regression probe: half-alpha red over blue reads
RGB(128,0,127), with underlying blue RGB(0,0,255). Linux Release tests pass
33,446 assertions in 463 cases. iOS validation was unavailable in this Linux
workspace.

## Retained evidence

- [N97 baseline](screenshots/n97-baseline.png)
- [X7 inside-Games installation complete](screenshots/x7-install-complete.png)
- [X7 map with controls](screenshots/x7-map-controls.png)
- [X7 map with controls hidden](screenshots/x7-map-hidden.png)

These emulator captures document the tested sample, not all N-Gage titles.
Game imagery retains its original owners' copyright.

## Device installation correction — 1.30(1542)

A Nokia 808 Belle FP2 user reported a compatibility warning followed by failure
with Install Server and Open4All patches enabled. The previous host-side SIS
installation check did not exercise the ROM SWI planner. Do not count the older
host-installed client as proof of phone installation.

The corrected package gives every controller an explicit Symbian^3/Belle target
list. This includes 15 original controllers and the two added native components;
empty SDK target lists can be rejected by the ROM's SISCompatibleIfNoTargetDevices
policy (see CRequisitesChecker::AnyOnDeviceL). It also recognizes the Belle ROM
marker, increments the wrapper revision to 1.30(1542), and excludes the X7 CFW
installation service from the ordinary-phone package. That backend is retained
only in `ngage-installer-x7-custom-rom.sis`.

The [native installer probe](native-installer/README.md) opens the real ROM
Software Installer. On a fresh isolated X7/Belle C/E profile, the corrected SIS
passed compatibility checks without a warning and completed installation. All
client/native files and SIS registry entries were created. The ordinary-phone
package does not install or replace YnstallServer. A real 808 retest is pending.

## Diagnostic logging and launcher removal — 1.30(1543)

An 808 user confirmed installation and Games startup, then reported KERN-EXEC 3
when launching Hooked. This release instruments the guest display DLL and adds
a hidden process watcher; it does not claim to fix that crash. The ordinary
Games icon remains the entry point. The helper component is now a single EXE,
retains its UID with version 1.0.1, and installs no menu/localization/icon files.
A native full upgrade from the prior component removed its old registration.

[Controlled panic check](diagnostic-panic/README.md) verified durable startup and
exit type/reason records. The native upgrade exposed EKA2L1's missing SIS registry
SID-to-filename IPC; the generic fix is in yeatse/EKA2L1 branch
fix/sisregistry-sid-filename (080d82ee2), with 464 tests / 33,456 assertions passed.
Normal and diagnostic packages are built separately in CI; only the diagnostic
variant writes phone files.

## Phone report and isolated loader checks — 1.30(1544)

The 808 supplied a complete controls.ready/game.watch/game.exit trace (panic type
2, reason 3, category KERN-EXEC), with no posting.log on either drive. This is not
proof that a particular display operation caused the crash. Added a three-phase
[loader self-test](diagnostic-selftest/README.md), triggered only after game panic
in ordinary diagnostic use. It leaves the game unmodified and catches failures
in separate processes. The display file fingerprint is included in build-report
so stale or different installed DLLs can be identified from phone records.

## 808 loader rejection — 1.30(1545)

The physical 808's 1544 probe returned -46 (KErrPermissionDenied) at the first
RLibrary::Load, before ordinal lookup/factory entry. Its 15,260-byte display
image fingerprint 53fda007 matched the delivered diagnostic package.
The display DLL had capabilities 0x000fffff, while the X7 ROM ws32/fbscli
headers declare 0x000ffffe. The original ngibackbuffer also uses 0x000ffffe.
The display replacement now uses ALL-TCB, without reducing any game EXE's
capabilities or modifying the ROM.

Symbian's [RLdrReq::CheckSecInfo](https://github.com/SymbianSource/oss.FCL.sf.os.kernelhwsrv/blob/master/userlibandfileserver/fileserver/sfile/sf_ldr.cpp)
requires the candidate to cover iPlatSecCaps; [E32Image::ProcessImports](https://github.com/SymbianSource/oss.FCL.sf.os.kernelhwsrv/blob/master/userlibandfileserver/fileserver/sfile/sf_lepoc.cpp)
sets that requirement from the importing image's iS.iCaps. A low-capability
probe process therefore does not exempt the importing display DLL's TCB edge.
EKA2L1 lacked this dynamic-library check; the independent branch
`fix/loader-capability-checks` adds it before exposing a library handle.

Validation used Linux Release EKA2L1 `573148ee2`: all 465 tests / 33,463
assertions passed. Its guest loader rejected the unchanged 1544 display image
with -46. The final 1545 diagnostic SIS then upgraded through the X7 ROM SWI;
the helper started with build marker 1545. Its self-test recorded capabilities
1048574, display fingerprint 713b6259, and successful load/factory/backbuffer
phases. All 17 installer host checks passed. These checks establish the loader
correction, not physical 808 gameplay or completion of later display operations.

## 808 playable report, controls and audio tracing — 1.30(1546)

The user confirms 1545 is playable on 808 Belle FP2, with no sound. Added L/R
softkeys, square translucent buttons and a geometric centered hide/show glyph.
The diagnostic audio import bridge forwards ROM MMF exports using ARM tail
branches: it restores r0-r3, the caller's stack and LR before bx to the resolved
entry. This preserves both hidden aggregate return pointers and the native
leave path. Code bytes of the two pinned NGI audio images remain identical after
bytepair expansion; only the import string and compression/header/SIS metadata
change. Regression checks cover both data units, descriptions, hashes, truncation
and unchanged instructions. The ordinary build does not instrument audio.

The diagnostic candidate upgraded through X7 ROM SWI in the native-install
profile; C-drive display, controls and audio bridge images and the redirected
E-drive NGI audio import were verified. UI/game checks use a separate existing
Hooked profile with those exact package images overlaid, because that older
fixture contains prototype component versions which cannot be normally upgraded
to the current component version sequence. Game cover launch reached the game;
L/R softkeys, square controls and hide/show were exercised, including R selecting
language/save-slot menus. Screenshots retain centered minus and plus glyphs.
The audio bridge logged ROM load 0, PCM16 initialization, configuration, PlayInit,
PlayData and volume request 7 / maximum 10 / current 5 then 7. This validates
forwarding and log capture, not physical 808 audio output or audible emulator
output (the Linux test environment uses an ALSA null output).

Final ordinary and diagnostic builds completed; all 20 host checks passed.

## Control placement — 1.30(1547)

Verified the package's new helper in the existing Hooked/X7 game fixture:
R at upper right, L at lower right, sharing A/B's x coordinate. Hide/show
remains at upper left, and hiding leaves only its centered plus visible.
Both ordinary and diagnostic packages were rebuilt.

## Phone volume and softkey mapping — 1.30(1548)

808's 1547 audio.log confirms repeated successful playback calls while NGI
requests volume 7 against MaxVolume 10000 (default/current initially 5000, then
7). The pinned profile's audioclipmaxvolume is 10. Added a shared integer volume
conversion used before restoring the MMF call's r1 argument. Host checks cover
mute, full scale, intermediate rounding, invalid ranges, and INT_MAX using an
independent 64-bit reference. In particular 7/10 -> 7000/10000; a maximum-10
backend preserves all legacy requests. Both normal and diagnostic packages now
include this compatibility path; only the latter writes logs.

The pinned N97 profile's orientation_0 and orientation_90 keytranslationdata
map scans 3 and 1 to softkey bits 0x01000000 and 0x02000000; scans 0xa4/0xa5
(Device0/Device1) are absent. L/R use Enter/Backspace accordingly, with diagnostic
send status and scan/event records. The X7 game fixture logs the new scan 3/1
events and the volume bridge's 7 -> 7 / MaxVolume 10 path. The 808's audible
output and softkey behavior still need physical retesting with this version.

Final normal/diagnostic builds completed and all 21 host checks passed.

## RemCon volume and actual virtual-softkey masks — 1.30(1549)

1548's audible output is confirmed on 808. Its L/R failure exposed a wrong
assumption: the N97 keyboard table entries for Enter/Backspace are not the
virtual softkey path. In the pinned ngiinput.dll, pointer hit handlers emit
0x1000/0x2000 to both standard and internal listeners. The augmented standard
lookup and internal lookup call now produce those masks for Device0/Device1
(scan 0xa4/0xa5 or key 0xf842/0xf843). Both original searches remain the fallback.
The pinned source SHA, vtable slot, original call and code relocation are
validated before changing the image. Compiler-generated Thumb code is executed
with Unicorn for both ABIs, both key representations, ordinary-key fallback,
stack and callee-saved-register preservation. Package tests check both copies,
exact bounded changes and unchanged tail sections. CI builds run these checks
with the cached SDK.

The Hooked/X7 game fixture reached the main menu using L, opened the exit
confirmation using R, and cancelled it using R. RemCon module loading and
registration returned zero in the game process. This environment does not
supply hardware/bearer RemCon commands, so the physical side buttons need an
808 test; native session/feature/message/response layouts follow SymbianSource
oss.FCL.sf.os.devicesrv remotecontrolfw client/inner/src/session.cpp,
common/remconserver.h, types/src/remconifdetails.cpp and coreapitarget.cpp.
The adapter uses non-leaving session calls and preserves audio if setup fails.
The SDK's RemCon address size and core operation/action constants are checked.

A source-guided EKA2L1 branch fix/remcon-session-version (de3d1e163) recognizes v2 session
roles from TVersion.build (controller 1, target 2), preserving the old handshake.
Unhandled requests now complete with NotSupported rather than hanging.
RemCon bearer delivery remains outside the emulator validation performed here.
All 466 Linux Release emulator tests / 33,469 assertions passed; all 24 host
checks passed with the SDK, including compiled adapter checks.

The final diagnostic bridge also logged remote.module.pin, remote.connect and
remote.register with result zero in the game process. Its callback module stays
loaded while the active receivers exist. Final normal and diagnostic packages
were rebuilt; physical volume-button delivery remains to be checked on 808.

## Verified release and diagnostic cleanup — 1.30(1550)

The user confirmed 1549 passes on Nokia 808 Belle FP2, including system volume
keys and NGI L/R. Release 1550 removes child crash probes, diagnostic process
monitoring, trace output/counters and the diagnostic build option. Native
compatibility behavior is retained. The component version advances to 1.0.8
and the root installer to 1.30(1550) for upgrades from the diagnostic package.
Old phone logs are left untouched. See docs/adaptation-summary.zh-CN.md.

The 1550 local source build and all 24 SDK-backed host checks passed. The final
SIS round-trips through the parser, and decompressed payloads contain none of the
removed probe command lines, self-test command, log-directory or trace markers.
Ordinary SIS SHA-256:
`669006d9d47628c3aa61add2ed3e4f48989d88efd08d4d77752676a227b6e315`.
This cleanup has not received a separate physical-phone retest yet.
