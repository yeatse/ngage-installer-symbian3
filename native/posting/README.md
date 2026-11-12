# Native display posting compatibility

This DLL implements Nokia's Display Posting API with the phone's existing
window server and bitmap server. It uses no EKA2L1 dispatch calls.

The three interface files `Posting_Surface.h`, `Posting_Surface_Factory.h` and
`posting_surface.inl` are Nokia's 2006 public interface, retained under EPL 1.0.
They come from `sf/os/mm/mm_plat/display_posting_api/inc` in
[cdaffara/symbiandump-os1](https://github.com/cdaffara/symbiandump-os1).
The corresponding license is included here. `main.cc`, CMake and build tooling
are this project's implementation.

Currently supported source formats are little-endian RGB565, RGB888 and RGBx888.
`KPostPlatformDependent` combined with an NGI pixel format (0x4010 RGB444,
0x8010 RGB555, 0x10010 RGB565, 0x20018 RGB888, 0x40020 RGBx888) is also accepted; the
installer's patched `ngibackbuffer.dll` posts formats that way.
Rotation and mirroring are reported as unsupported. `Destroy` releases posting
buffers while retaining the presentation objects so that `InitializeL` can be
called again on the same surface.

Hooked has passed emulator gameplay checks and user validation on Nokia 808 Belle FP2.

Presentation converts frames into a software RGB picture. Where the phone has
the Symbian^3 surface manager, the picture is written straight into a mapped
XRGB compositor surface that is the background surface of the game's own
window, and each frame is handed over with
`RSurfaceUpdateSession::SubmitUpdate`. The window comes from
`ngibackbuffer.dll`: the installer patches its two `NewPostingSurfaceL` calls to
pass the address of the `RWindow` it already holds for direct screen access in
place of display 0. Nothing covers the game's window, so its visibility and DSA
region stay as the game expects. The surface is kept when the game initialises
a new source. This path is experimental and not yet validated on a phone.

If no window was passed or any surface call fails, the picture is blitted into the window server's DSA
buffer through `CFbsScreenDevice`, as `CDirectScreenAccess` does. The window
server shows that buffer only inside the game's running DSA region, and the
region passed to `SetClipRegion` further clips each frame. No emulator-specific
library replacement is used.

`include/` holds the surface headers and `defs/` the frozen exports of
`surfacemanager.dll` and `surfaceupdateclient.dll` from the Symbian Foundation
graphics package (EPL 1.0); the build makes import libraries from the latter.
The first 16 original factory/buffer exports retain their ordinal ABI.

The factory starts the background touch-control service without bringing
another application to the foreground. Release 1550 removes diagnostic
logging and crash probes.
