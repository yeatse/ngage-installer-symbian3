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
Rotation and mirroring are reported as unsupported. `Destroy` releases posting
buffers while retaining the presentation objects so that `InitializeL` can be
called again on the same surface.

Hooked has passed emulator gameplay checks and user validation on Nokia 808 Belle FP2.

Presentation converts frames into a software RGB bitmap and blits it into the
window server's DSA buffer through `CFbsScreenDevice`, as `CDirectScreenAccess`
does. The window server shows that buffer only inside the game's running DSA
region, so overlapping windows such as the touch controls stay on top. The
region passed to `SetClipRegion` further clips each frame. No separate
presentation window is created: it would cover the game window and empty its
DSA region. No emulator-specific library replacement is used.
The first 16 original factory/buffer exports retain their ordinal ABI.

The factory starts the background touch-control service without bringing
another application to the foreground. Release 1550 removes diagnostic
logging and crash probes.
