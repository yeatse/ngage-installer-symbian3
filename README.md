# N-Gage installer for Symbian^3

Work in progress: unsigned N97 N-Gage 1.30(1541) client adaptation for already
hacked Symbian^3 phones. The supplied X7/Belle ROM has passed inside-Games installation and
boat-map gameplay with the supplied sample; physical phones have not been tested.

The installer selects the original N97 touch profile on Symbian^3, preserves
the original client binaries, and adds a native **Games (Touch)** launcher.
During installed games it shows translucent five-way controls on the left,
A/B (5/7) on the right, and a visibility toggle. The overlay needs AllFiles
to discover installed game metadata and SwEvent to send key events.

Put legally obtained `.n-gage` files in `E:\n-gage`, open **Games (Touch)**,
install inside Games, then launch the game there. The package includes an adapted Belle `YnstallServer` installation service at
`C:\sys\bin\YnstallServer.exe`; installing it replaces a service at that path.
This targets the supplied hacked X7/Belle environment whose `uiss.dll` already
requests that service. It does not patch the ROM's SWI client or provide game
licenses. Other hacked configurations and physical phones require validation.

The service is a pinned public patched Belle binary, with a narrowly scoped
source-controlled binary adaptation: a filename-less `MSisDataProvider` may
return `KErrNotFound` from `GetSisFileNameL`. Other errors remain fatal.
The original Nokia Games client and game payloads are preserved.

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

Output: `artifacts/ngage-installer-symbian3.sis` and `build-report.json`.
The package is unsigned. SDK-generated experimental E32 identities and
capabilities are updated with verified header checksums; executable code stays
unchanged. Original Nokia components retain their upstream identities.

See [validation](validation/README.md) for fixtures and acceptance steps.
Upstream Nokia/game material is fetched separately and is not covered by this
repository's MIT license. No ROM or game is included in the build artifact.
