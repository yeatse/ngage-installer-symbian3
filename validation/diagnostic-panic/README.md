# Controlled exit-recording check

For an isolated emulator only; this probe is not in the phone SIS. Build/package
with the same SDK setup as validation/native-installer. Install the diagnostic
N-Gage package, and create the empty fixture directory
`C:\private\20007b39\game3758098836` so the generic game watcher recognizes
this probe SID (0xe0000994). Launch its app UID 0xe0000994. It loads the real guest
display DLL factory (also testing background-service startup), waits 10 seconds,
and intentionally panics with category KERN-EXEC/reason 3.

Expect posting.log to contain factory.entry and touch.log to contain game.watch
followed by game.exit with type 2/reason 3. Remove that empty fixture directory
afterward. The test does not call the surface API without a valid Symbian trap
frame, and does not represent running the actual Hooked title on an 808.

Verified exit type/reason records on X7. The emulator returned category None for
this process panic and later aborted during host teardown; phone category values
must come from its real RProcess API. The previously completed upgrade/icon
removal and the persisted records are independent observations.
