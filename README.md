# N-Gage installer for Symbian^3

Work in progress: unsigned N97 N-Gage 1.30(1541) client adaptation for already
hacked Symbian^3 phones. The supplied X7/Belle ROM has passed inside-Games installation and
boat-map gameplay with the supplied sample; physical phones have not been tested.

The installer selects the original N97 touch profile on Symbian^3, preserves
the original client binaries, and adds a background touch-control service.
During installed games it shows translucent five-way controls on the left,
A/B (5/7) on the right, and a visibility toggle. The overlay needs AllFiles
to discover installed game metadata and SwEvent to send key events.

Put legally obtained `.n-gage` files in `E:\n-gage`, open **Games**,
install inside Games, then launch the game there.

Use **ngage-installer-symbian3.sis** on ordinary hacked Symbian^3/Belle phones,
including the Nokia 808 Belle FP2 configuration with Install Server and Open4All.
This variant uses the phone's existing installation service.

**ngage-installer-x7-custom-rom.sis** additionally replaces
`C:\sys\bin\YnstallServer.exe`. It is reserved for the supplied X7 custom ROM
whose `uiss.dll` requests that service. It is not the package to use on an 808.
The pinned public Belle service has a narrowly scoped adaptation for filename-less
MSisDataProvider streams. No game license is supplied; original client/game
binaries are preserved.

Package revision 1.30(1542) corrects legacy and empty component platform declarations and
recognizes both Symbian^3 and Belle product/ROM markers. Physical-phone
installation is under investigation following an 808 failure report; emulator
host-side SIS installation alone is not sufficient validation.

## Build

The **Build unsigned Symbian installer** Actions workflow builds the ARM native
components from source using [symbian-platform](https://github.com/hpnkv/symbian-platform)
0.3.0 and uploads `ngage-installer-symbian3-unsigned`. Large inputs are cached
individually by SHA-256 and verified after every restore. Normal builds do not
download ROMs or games.

For a local Linux build, extract the pinned SDK from `validation/sdk.json`, then:

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements-build.txt
python3 tools/fetch_inputs.py downloads --only n97-ngage.sisx
python3 tools/fetch_inputs.py downloads --manifest validation/hacked-x7.json \
  --only ynstallserver-belle-fixture.exe
.venv/bin/python tools/build.py --sdk /absolute/path/to/sdk --installer downloads/n97-ngage.sisx
```

Output: the ordinary-phone SIS and report, plus the separately named X7 custom-ROM
SIS and `build-report-x7-custom-rom.json`.
The package is unsigned. SDK-generated experimental E32 identities and
capabilities are updated with verified header checksums; executable code stays
unchanged. Original Nokia components retain their upstream identities.

See [validation](validation/README.md) for fixtures and acceptance steps.
Upstream Nokia/game material is fetched separately and is not covered by this
repository's MIT license. No ROM or game is included in the build artifact.

## Diagnostic package — 1.30(1543)

Use `ngage-installer-symbian3-diagnostic.sis` for the Nokia 808 crash investigation.
No Games (Touch) menu registration or icon is installed. Its existing component
UID is retained with version 1.0.1 so a native full upgrade removes the old menu
resources. The service starts during installation and is also started
on demand by the display compatibility DLL. Open the original Games icon.

Reproduce the failure once immediately after installing, then collect all files
in `E:\logs\ngage\` (fallback: `C:\logs\ngage\`). `posting.log` records
factory/construction, allocation, pixel access, drawing and submission boundaries;
`touch.log` watches installed-game processes and records exit type, reason and
category. Each record is flushed and the file is closed; output is throttled after
the first three frames. Logs append across runs. They contain timing, IDs, process
names and rendering parameters, not a full CPU register dump.

The diagnostic flag is `python tools/build.py ... --diagnostic`; Actions builds
both ordinary and diagnostic variants using the same cached inputs. The normal
build disables diagnostic file output. This instrumentation does not establish
that the reported phone crash is fixed.
