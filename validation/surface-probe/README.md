# Surface probe

A standalone app (UID 0xe000099a, no capabilities) that exercises what the
posting DLL needs on a phone, so a failure there can be located without a full
installer round trip. Not part of the phone SIS. Build and package like
validation/rg-load-probe; the SIS is unsigned.

Install it next to the N-Gage package under test and open "Surface probe". It
appends to `C:\Data\surface_probe.txt` and runs each stage in its own thread, so
a panic ends only that stage and its exit type, reason and category are logged:

0. loads the surface, posting and game display DLLs by name and logs where each
   was found;
1. the compositor surface path by ordinal on a window of its own: three colour
   bars for four seconds;
2. to 4. a bitmap device, context and font, as the posting DLL's overlay uses;
5. the installed PostingSurfaceFactory.dll handed the window, four chequered
   frames;
6. the same with display 0, the call an unpatched ngibackbuffer.dll makes.

ROM code traps its own leaves, so the probe is built with native leaves; without
them the first trapped leave inside bitgdi terminates the thread.

On the X7 in EKA2L1 every stage ends with `leave=0` and exit type 0.
