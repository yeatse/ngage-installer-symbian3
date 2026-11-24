# Video probe

A standalone app (UID 0xe00009a4, package 0xe00009a5, no capabilities), not part
of the phone SIS. Asphalt 4 ends with KERN-EXEC 3 on a Nokia 808 when its intro
video starts. Its `ngivideo.dll` creates `CVideoPlayerUtility` on the game window
inside a trap. If that leaves, it frees its runtime service pointer (`+0x5c`), and
its next display-window call (link address `0x837e`) dereferences that pointer.
The probe plays the same video with the system video player on a window in each
state the game window can be in, to find which one makes the player fail.

It plays `C:\Data\video_ngage.mp4` (also tried on `E:`/`F:`, in `\Data\`, and in
`\Private\2001092B\`, which can be read with Open4All), for up to eight seconds per
stage, and appends to `C:\Data\video_probe.txt`. Each stage runs in its own thread
on a new full-screen window:

0. nothing else on the window;
1. a running direct screen access (DSA), as the game's `ngibackbuffer.dll` has;
2. a compositor background surface, set as the posting DLL sets it;
3. DSA, then the surface;
4. DSA and the installed `PostingSurfaceFactory.dll` after one frame, the state
   of the game window at the intro;
5. the same, with the posting surface destroyed before the video.

For each stage it logs the `NewL`, open, prepare and play results.

In EKA2L1 (X7) every stage plays the video. The emulator's own
`mediaclientvideo` then faults when the player is deleted, so `player deleted` and
`leave=` are missing there.
