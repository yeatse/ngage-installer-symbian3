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

Where the phone has the Symbian^3 surface manager, each frame is converted to
XRGB at the source's own size in a mapped compositor surface that is the
background surface of the game's own window, and handed over with
`RSurfaceUpdateSession::SubmitUpdate`. The surface configuration's extent and
viewport carry the posting rectangles, so the compositor does the scaling; the
window's background colour is set to black for the area around the extent. The
window comes from `ngibackbuffer.dll`: the installer patches its two
`NewPostingSurfaceL` calls to pass the address of the `RWindow` it already
holds for direct screen access in place of display 0.

The touch controls are not part of that picture. The window server gives
per-pixel alpha only to the UI layer, not to a window's background surface, so a
second surface cannot be blended over the game. The posting surface sets a
property of the game's SID (`kTouchCompositorKey`) and the touch service then
draws the controls in a transparent window of its own over the screen, which
the compositor blends over the game surface. That window covers the game's
window, so the game's DSA region is empty while it is up; the game does not
draw through it on this path.

The surface is kept when the game initialises the same source size again; it is
remade for a new source. The window server implements direct screen access as a background
surface of the same window, and on the 808 `CDirectScreenAccess::StartL` fails
while another surface is there; `ngibackbuffer.dll` then passes a null region to
`SetClipRegion`. So the surface is set with the first frame after each
initialisation, when the game's DSA is running, and removed in `Destroy`, which
the game calls when its DSA is aborted. Compositor scaling and the control
window are validated on the X7 in EKA2L1 only.

If no window was passed or any surface call fails, frames are scaled in software
into a picture of the screen's size, the controls are blended into it, and it is blitted into the window server's DSA
buffer through `CFbsScreenDevice`, as `CDirectScreenAccess` does. The window
server shows that buffer only inside the game's running DSA region, and the
region passed to `SetClipRegion` further clips each frame. No emulator-specific
library replacement is used.

`include/` holds the surface headers from the Symbian Foundation graphics
package (EPL 1.0). `surfacemanager.dll` and `surfaceupdateclient.dll` are not
linked: they are loaded by name when the first source is initialised and called
by their frozen export ordinals, so a phone where they cannot be loaded still
starts the game and uses the DSA path. `RWindowBase::SetBackgroundSurface` and
`RemoveBackgroundSurface` are called the same way through ws32 ordinals 598 and
600: the Nokia 808 resolves both, yet its loader refuses any image that imports
ordinal 598 with KErrNotSupported. The row stride comes from `SurfaceInfo`; the
808 pads a 360 pixel wide surface to 1472 bytes.
The first 16 original factory/buffer exports retain their ordinal ABI.

The factory starts the background touch-control service without bringing
another application to the foreground. Release 1550 removes diagnostic
logging and crash probes.
