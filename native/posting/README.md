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

Validation is ongoing; no physical phone has been tested.

Presentation uses a non-focusable window and a software RGB bitmap. The public
API's optional DSA clipping method returns `KErrNotSupported`: the window server
clips and composes the presentation window, including translucent controls.
No direct framebuffer access or emulator-specific library replacement is used.
The first 16 original factory/buffer exports retain their ordinal ABI.

The diagnostic build includes synchronous filesystem stage records from
`native/common/trace.h`. The factory starts the background control/watch service
without bringing another application to the foreground. No exception handler is
installed, and the original panic/exit behavior is retained for diagnosis.
