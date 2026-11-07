# N-Gage 2.0 adaptation for Symbian^3

The delivered version is 1.30(1551), based on the N97 N-Gage 1.30(1541) installer.
User testing on a Nokia 808 Belle FP2 with Install Server and Open4All confirmed
working gameplay, sound, hardware volume buttons and L/R softkeys. Emulator
validation used a locally built Linux EKA2L1 with N97 and X7 ROMs.
The tested game is Hooked: Creatures of the Deep; this does not establish
compatibility with every N-Gage game or phone model.

## Installation and launch

The adaptation corrects legacy SIS component platform dependencies and empty
platform declarations, recognizes Symbian^3/Belle, and selects the original N97
touch profile on newer systems. Component UIDs, versions, E32 header checksums
and SIS hashes are updated consistently so hacked phones can upgrade an existing
installation without signing the package.

The original Games workflow is retained: copy `.n-gage` files to `E:\n-gage`,
install them inside Games, then launch them from their covers. The touch service
runs only while its owning game process exists, without registering an extra
Games(Touch) launcher icon. The display DLL passes its process ID to the helper,
which subscribes to `RProcess::Logon` and exits when the game ends. The installer
does not start the helper. A per-game mutex prevents duplicates while allowing
separate games to have independent helpers. Focus checks use the actual owner
process ID, so switching to another app hides controls without ending the service.
The ordinary package uses the phone's existing installation service. A separate
package provides installation-service compatibility for the supplied X7 custom
ROM, avoiding replacement of the existing service on an 808.

## Display and touch controls

A native Display Posting API compatibility DLL presents the game through the
phone's existing window and bitmap services. Frames are drawn into the window
server's direct screen access buffer, which is composed only inside the game's
running DSA region; a separate presentation window would cover the game window,
empty that region and stop games that only post while it is non-empty. It
supports buffer submission, DSA clipping, scaling and RGB565/RGB888/RGBx888
conversion. Its capabilities are set
to ALL-TCB to match the window/bitmap dependencies, resolving the loader's -46
permission failure that preceded the crash on the physical phone. The phone
components use existing ROM interfaces.

Portrait games keep the phone in landscape. Only the picture is portrait,
unrotated, scaled to the full screen height and centred; for example, 240×320
is shown at 270×360 from x=185 on a 640×360 screen. The installer rewrites the
N97 display configuration selected on Symbian^3. That configuration enables the
portrait option and scales portrait backbuffers to the screen height. It also reports the native
display as 240×320, matching its portrait orientation at angle 0; games such as
Reset Generation lay out for the native size and otherwise draw only the top 240
lines of a 240×320 buffer. Two pinned NGI instructions
are changed. `ngigameorientation.dll` requests the landscape app UI
orientation for every game orientation instead of selecting a window-server
screen mode by configured index, which is device-specific (on a physical 808 it
selected portrait), and `ngibackbuffer.dll` keeps portrait buffer dimensions instead
of swapping them.

Translucent square controls provide a five-way pad on the left, R/A/B/L from top
to bottom on the right, and hide/show at the upper left. Input handling tracks
holds, repeats and multiple pointers, and releases keys when controls are hidden
or focus changes to prevent stuck keys.

A and B send Enter and Backspace, the primary and secondary action keys of
the N97 NGI input profile that Symbian^3 selects. That profile has no keypad
digits, so phone-keypad keys such as 5 and 7 reach the game unmapped.

L/R cannot be implemented simply by sending Enter/Backspace. Following NGI's
original pointer path, a source-built Thumb adapter augments the pinned
`ngiinput.dll`'s standard 32-bit and internal 64-bit lookups. It maps
Device0/Device1 to the actual logical softkey bits 0x1000/0x2000, retaining the
original lookup for other keys. Builds validate the upstream SHA, instruction
locations, relocation and both package copies, then execute the adapter on an
ARM CPU to verify its ABI behavior.

## Audio and system volume

The NGI audio library's MMF import is redirected to a source-built bridge DLL,
which forwards calls to ROM DevSound. The original NGI audio instructions,
arguments, stack and return conventions are preserved.

Phone logs showed NGI requesting volume 7 while the device maximum was 10000,
making playback almost inaudible when the value was passed through directly.
The bridge scales the legacy 0–10 range to the actual device range: for example,
7 becomes 7000. On a maximum-10 backend it stays 7, and mute stays zero.

A RemCon Core API target in the game's audio thread supports hardware volume
click, press and release actions with bounded hold repeat. It adjusts the active
DevSound instance directly. The bridge handles pause/resume and changes to the
game's own volume setting, clears the instance reference on Stop, and keeps the
callback DLL loaded while receivers exist to avoid dangling object or code
references.

## Licence status

Games installed from exported packages have no DRM rights object for their
content ID, and new rights can no longer be issued. The client's
`gametitlemetadata.dll` then falls back to the installed-parts state, which the
Symbian^3 Games UI and PlayServer report as a trial: Games shows a Trial ribbon
and the game reports a trial version. The S60 3rd Edition client computes the same
state but does not present it as a trial.

The build retargets one Thumb branch in the client's licence query so that "no
DRM permission" takes the existing valid-rights path. Expired or limited rights
objects and missing game parts are still reported as before. The patch is
applied only to the pinned DLL image and is verified by hash and instruction
bytes.

## Build and validation

The project uses symbian-platform 0.3.0 to compile native components from source
on Linux and produce unsigned SIS packages. GitHub Actions builds and uploads
the artifacts. Large installer, SDK and X7 service inputs are cached by SHA-256
and verified after restoration. Regular builds do not download ROMs or games.

The 31 project checks cover package structure, original-input protection,
import redirection, component identities, volume scaling, the licence-query
patch, portrait display settings and instructions, and Thumb softkey execution
and ABI behavior. Runtime validation covers the N97 baseline,
installation and launch inside Games on X7, game menus and gameplay screens,
and user testing on the physical 808.

EKA2L1 contract issues encountered during the work were fixed on separate
branches, including installation IPC, dynamic-library capability checks and
RemCon v2 session-role detection. No emulator-specific interfaces were added to
the phone components. The last emulator regression run passed 466 tests with
33,469 assertions. See the [validation history](../validation/README.md) and
EKA2L1 branch history for commits and evidence.

1550 removes crash child-process probes, process-exit monitoring, filesystem
logging, diagnostic counters and the diagnostic build option. It retains the
compatibility behavior validated in 1549. Old phone logs are not deleted
automatically. Upgrading requires a reboot to replace resident services and DLLs.
