# N-Gage 2.0 adaptation for Symbian^3

The delivered version is 1.30(1554), based on the N97 N-Gage 1.30(1541) installer.
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
The compatibility files are installed as files of the N-Gage package itself
rather than as embedded packages, so the phone lists one removable item. When
upgrading from an earlier build, the installer removes the embedded packages
that the new N-Gage package no longer contains before checking for overwritten
files, so their files pass to the N-Gage package.

## Display and touch controls

A native Display Posting API compatibility DLL presents the game through the
phone's existing window and bitmap services. Frames are drawn into the window
server's direct screen access buffer, which is composed only inside the game's
running DSA region; a separate presentation window would cover the game window,
empty that region and stop games that only post while it is non-empty. It
supports buffer submission, DSA clipping, scaling and RGB444/RGB565/RGB888/RGBx888
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
lines of a 240×320 buffer. The N97 configuration lists only landscape display
configurations, so it also gains 240×320 ones in both of its colour formats; games such
as Space Impact Kappa Base look for a display configuration of the portrait native size
and quit right after start-up without one. Pinned NGI instructions
are changed. `ngigameorientation.dll` requests the landscape app UI
orientation for every game orientation instead of selecting a window-server
screen mode by configured index, which is device-specific (on a physical 808 it
selected portrait). Because the window server then stays landscape, the
display never reported portrait and games that request it after start-up, such
as Mile High Pinball and Tetris, kept a landscape backbuffer: Mile High Pinball
stopped at its splash screen and Tetris was drawn off-centre and cut off. A
source-built Thumb stub in the unused mode-selection code now posts a screen
device change event to the game's own window group when a request switches
between portrait and landscape, and `ngidisplay.dll` reports the requested game
orientation instead of the window server's rotation, so NGI display observers
recreate a portrait backbuffer. The stub uses two existing window-server
imports, retargeted to `RWindowGroup::Identifier` and
`RWsSession::SendEventToWindowGroup`; both stubs are executed on an ARM CPU
during the build to verify their calls and results. `ngibackbuffer.dll` keeps portrait buffer dimensions instead
of swapping them, and sizes every 16-bit backbuffer (RGB444, RGB555, RGB565) at two
bytes per pixel, posting it as `KPostPlatformDependent` with the NGI format value.
Unpatched, it handled only RGB565 as 16-bit, so Snakes Subsonic's RGB444 picture was
posted as RGBx888 and appeared colour-shifted and doubled side by side.

DChoc Café Solitaire ended with KERN-EXEC 3 at start-up on a physical 808. It
requests portrait while it is still initialising and pumps display events until
the display change arrives. `ngidisplay.dll` also tells display observers when
the focused game window becomes visible, and on the 808 that event is queued
ahead of the display change; the game's handler then uses objects it has not
created yet. A third stub, in the window-server rotation query that the display
stub leaves unreachable, drops that notification once when the requested
orientation is portrait. Landscape games and later notifications are unaffected.
The crash was located with a temporary sampling probe on the phone, because the
emulator delivers the visibility event later.

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
click, press and release actions with bounded hold repeat. A key press only
records the new level; the bridge applies it to each DevSound instance the next
time the game calls PlayData or PlayInitL on that instance. NGI deletes DevSound
through its virtual destructor, which never passes through the bridge, so a
remembered instance could already be freed when a key arrives; adjusting it
directly made Tetris exit with KERN-EXEC 0 on the 808. The bridge handles
changes to the game's own volume setting and keeps the callback DLL loaded while
receivers exist to avoid dangling code references.

## Game memory pools

Games such as Worms World Party, FIFA 08 and FIFA 09 failed at start with
"Close some applications" (NGI error -1000002, mapped from `KErrNoMemory`) on
Symbian^3, although the phone had plenty of free RAM. Two assumptions in
`ngimemorymanagement.dll` about the S60 3rd Edition `RHeap` do not hold for the
Symbian^3 `RHybridHeap`:

- The pool ownership check reads the heap's first and top cell bounds.
  `RHybridHeap` sets the top only at construction, so cells past the first page
  are treated as foreign, and `ReAlloc`/`Free` on them fail. The check now uses
  the heap's chunk base up to the heap plus its maximum length.
- A pool is reserved by growing its first cell with `ReAlloc(ENeverMove)`.
  `RHybridHeap::DlRealloc` grows a cell in place only into memory that is
  already committed and never extends the chunk. The chunk heap behind a pool
  is therefore created fully committed (minimum length equal to the maximum),
  which is the memory the reservation would have committed anyway.

Both changes are applied only to the pinned DLL image and are verified by hash
and instruction bytes.

## Legacy thread heap

Brothers In Arms crashed while loading on Symbian^3 but not on S60 3rd Edition.
The Symbian^3 `RHybridHeap` serves cells of 64 KB and more from freshly
committed pages and small cells from a slab allocator, while the S60 3rd
Edition `RHeap` is a single Doug Lea heap that reuses freed memory. The game
depends on the old allocation pattern. Giving the game executable a fixed-size
heap (which makes `RHybridHeap` Doug Lea-only) fixed it, but that edits game
files.

Instead, the NGI `runtime.dll` that every game statically imports now imports
`ngheap.dll` in place of `estlib.dll`. The only ESTLIB import, `CloseSTDLIB`, is
forwarded unchanged. When the process attaches, `ngheap.dll` switches the main
thread to a Doug Lea heap placed inside the original heap's own chunk:

- In a disconnected chunk, `RHybridHeap` sits in the middle and keeps the
  lower half for its page allocator. That half is still unused when the
  process attaches, because only a few small cells exist and slabs start after
  64 KB of allocations. The new heap fills it with a non-zero offset, which
  makes it Doug Lea-only. A heap in a separate chunk did not work: Snakes
  Subsonic crashed when its heap addresses were above 64 MB, and a new local
  chunk lands after the 34 MB thread heap and Open C heap reservations.
- The heap is fully committed, like a fixed-size heap. With a growing heap,
  Snakes Subsonic read memory that the heap had already returned to the
  kernel.
- Cells allocated before the switch are still freed by the original heap. If
  one of them grows, it moves to the new heap, so the original heap never
  allocates again.
- The heap exposes its chunk through `iHandles` with a zero handle count, which
  the memory-pool ownership check above reads.

If the original heap is already Doug Lea-only, or the new heap cannot be
committed, the game keeps its original heap.

The switch applies only to games listed by secure ID in `ngheap.dll`, currently
Brothers In Arms. Applied to every game, Snakes Subsonic sometimes crashed on a
null virtual table after creating its audio buffer chunk. Other games run
correctly on the hybrid heap.

Only the import name in the two pinned `runtime.dll` copies changes; the code is
verified to be identical.

## DRM content size

Tomb Raider Underworld ended with KERN-EXEC 3 at Lara's first dialog. The game
sizes `lara.ani`, an OMA DRM v2 file with unencrypted content, with
`Seek(1, end)` on `ngifileaccess.dll`, then allocates and reads the result plus
one byte. The library forwards this to `CData::Seek(ESeekEnd, pos)`. On the X7
the Symbian^3 OMA agent returned position 1 instead of the content length, so
the game read 2 bytes and parsed the uninitialised remainder of its buffer as
animation records, corrupting its memory pool.

The pinned library now resolves end-relative seeks itself: it adds the offset to
the content size it already obtained from `DataSizeL` when opening the file,
clamps the result to that size (the agent does not clamp absolute positions) and
issues an `ESeekStart` seek. Start and current-relative seeks are unchanged.

## Game data recognition

Dogz, Dirk Dagger, Metal Gear Solid Mobile and Resident Evil Degeneration
crashed or reported that their encrypted assets could not be loaded. Their
`<game>fileaccess.dll` opens each `.ngdat` asset through CAF. Unprotected
files are served by the F32 agent, and the fileaccess component fails the open
unless the agent's MIME type query succeeds. The F32 agent answers that query
with `RApaLsSession::AppForDocument`, which returns `KErrNotFound` when no
recognizer claims the file. No ROM recognizer knows `.ngdat`, and on Symbian^3
the recognizers run in `EMimeServer`, which cannot read the game's private
directory.

A source-built ECOM recognizer, `ngdatrecognizer.dll`, assigns
`application/x-ngage-data` to `.ngdat` names with probable confidence, without
reading the file. Worms World Party hit the same failure with the `.dir` and
`.dat` archives it opens through `ngifileaccess.dll`: the failed open left the
archive unloaded and Quick Game crashed with KERN-EXEC 3. The recognizer
therefore also claims any other file under `\private\` with possible
confidence. Recognizers that identify the content, including the DRM
recognizer for protected DCF files, still win with higher confidence. The 5320
runs the S60v3 client and did not show either problem. The DLL's capabilities
are ALL-TCB, so it loads into the recognition server.

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

The 35 project checks cover package structure, multi-file components,
original-input protection, import redirection, component identities, volume
scaling, the licence-query patch, the memory-pool patch, portrait display
settings and instructions, and execution and ABI behavior of the Thumb softkey, orientation and visibility stubs. Runtime validation covers the N97 baseline,
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
