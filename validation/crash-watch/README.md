# Crash watch

A standalone diagnostic app (UID 0xe00009a2, package 0xe00009a3, no
capabilities), not part of the phone SIS. It is for a game that ends with a
panic on a phone while it runs in the emulator, e.g. Asphalt 4 with
KERN-EXEC 3 at start.

Opening "N-Gage crash watch" starts a detached copy of itself and returns, so
closing apps does not end the watch. A second copy exits at once
(`NgageCrashWatch` mutex). It watches every process started from C:, E: or F:
after it, and appends to `C:\Data\ngage_crash.txt`:

- `process`: SID, image and the code base the exe was loaded at, so an exe pc
  maps to link address `0x8000 + (pc - code)`;
- `lib`: each RAM-loaded library, with its ordinal 1 address and lowest/highest
  export. The library is kept loaded, so its code stays where the game had it;
- `thread.exit`/`process.exit`: exit type, reason and category;
- for a panic: the last 16 distinct pc/lr pairs sampled every 50 ms (`trail`),
  the register context, ROM words around a ROM pc, and the nearest ROM or RAM
  library export below each trail address (`near tN`, pc at even N, lr at odd N)
  and register (`near rN`);
- free RAM at start, while running, and at exit.

On a Belle phone with about 300 processes, build 1 ran out of baseline, watch and
library slots before the game started; build 2 has room for 1024 processes, 40
watched processes and 384 libraries, and checks each library name only once.

It stops after 20 minutes with no watched process. Build and package like
validation/rg-load-probe. For the emulator, add `-DCRASH_WATCH_TEST` to the
compile flags: the launcher then also starts PlayServer and a child that writes
to address 4, because the emulator's control API ends the previous app when it
launches Games. EKA2L1 does not implement `RThread::Context` and reports
thread exit reasons wrongly, so `trail`, `regs` and `near` only come from a phone.

On the X7 emulator profile Asphalt 4 started from Games with the watch running.
The log has the code bases of the game and of Games' processes and 47 RAM libraries.
The test child's exit was recorded as process exit type 1 (panic), reason 3.
