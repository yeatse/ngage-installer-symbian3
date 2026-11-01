# Isolated loader probe self-test

For an isolated emulator, not part of the phone SIS. Build/package using the
same SDK setup as native-installer. Stop any existing background helper, install
the diagnostic candidate, then launch this app (UID 0xe0000996). It starts the
headless helper with `--self-test`; normal phone use runs the same probes only
after the first observed game panic.

Expect three separate child PIDs in probe.log and three successful probe.exit
records in touch.log: explicit C-drive display DLL load/ordinal lookup, its
factory call/deletion, then explicit E-drive ngibackbuffer.dll load without
calling any exports. Each child can fail independently; later phases stop on
error. Children are monitored by polling their owned process handles, bounded
by a 20-second timeout, without suspending or injecting into a game.

The file header UID/version/flags/CPU values and FNV-1a file fingerprint come
from the actual disk image. The RLibrary Type/FileName results are separate
loader observations (EKA2L1 currently reports zero UIDs from RLibrary::Type;
use image.uids as the on-disk identity).

On X7 all three child phases returned normally, leaving the background service
running. The probe does not establish which library fails inside the 808 game.

The final diagnostic SIS was upgraded through the X7 ROM installer. All three
probe phases then returned zero; the display image fingerprint was 53fda007,
matching the package report. With only the installed backbuffer DLL temporarily
renamed, phase three logged image.open/load.return -1 and the parent recorded
probe.exit reason -1 without exiting. The DLL was restored after this check.
All 17 host package/service checks passed. The self-test launcher is excluded
from both delivery packages, which retain the hidden helper.
