# N-Gage installer for Symbian^3

Unsigned adaptation of Nokia N97's N-Gage 2.0 1.30(1541) installer for already
hacked Symbian^3/Belle phones. Current package: **1.30(1550)**.
Hooked: Creatures of the Deep has passed the N97/X7 EKA2L1 flow and user testing
on **Nokia 808 Belle FP2 with Install Server and Open4All**. Other games and
phone models have not all been verified.

Install `ngage-installer-symbian3.sis`, put legally obtained `.n-gage` files in
`E:\n-gage`, open **Games**, install the game there, and launch it from Games.
For an upgrade, exit Games, cover-install, then reboot to replace resident DLLs
and the background control service. No signing is required on the hacked phone.

The overlay provides translucent square five-way controls on the left and
R/A/B/L vertically on the right. Hide/show is at the upper left. L/R are native
NGI logical softkeys; the phone's hardware volume buttons adjust game audio.
Only the original Games icon is installed.

## What the adaptation changes

- Platform selection and SIS component declarations recognize Symbian^3/Belle
  and select the original N97 touch profile.
- A source-built Display Posting API DLL uses native window/bitmap services for
  presentation, scaling and RGB conversion. Its ALL-TCB capabilities match its
  dependencies and avoid the loader permission failure seen on 808.
- A background service discovers installed games and forwards touch controls to
  the foreground game, including held keys and key release on hide/focus loss.
- A source-built Thumb adapter extends the pinned `ngiinput.dll`'s 32/64-bit
  lookup paths with the actual NGI virtual softkey masks, retaining other keys.
- A native audio bridge forwards to ROM DevSound, scales NGI volume 0–10 to the
  device's actual range, and receives system volume press/release/click commands
  through RemCon. Callback code stays loaded while its receivers exist.
- E32 identities, capabilities, checksums and SIS hashes are updated consistently.
  Existing game binaries and ROM files are not modified.

1550 removes crash child-process probes, process-exit monitoring, filesystem
logging, diagnostic counters and the diagnostic build variant. It retains the
compatibility behavior validated in 1549. Old `E:\logs\ngage` / `C:\logs\ngage`
files are not deleted automatically and can be removed manually.

`ngage-installer-x7-custom-rom.sis` additionally replaces
`C:\sys\bin\YnstallServer.exe`. It is reserved for the supplied X7 custom ROM,
whose `uiss.dll` requests that service. **Use the ordinary package on an 808.**
The pinned Belle service has a scoped adaptation for filename-less
MSisDataProvider streams. The ordinary package uses the phone's existing
installation service.

## Build

GitHub Actions builds the native ARM components from source using
[symbian-platform](https://github.com/hpnkv/symbian-platform) 0.3.0 and uploads
`ngage-installer-symbian3-unsigned`. Large inputs are cached individually by
SHA-256 and checked after restoration. Builds do not download ROMs or games.

For a local Linux build, extract the pinned SDK from `validation/sdk.json`, then:

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements-build.txt
.venv/bin/python tools/fetch_inputs.py downloads --only n97-ngage.sisx
.venv/bin/python tools/fetch_inputs.py downloads --manifest validation/hacked-x7.json \
  --only ynstallserver-belle-fixture.exe
.venv/bin/python tools/build.py --sdk /absolute/path/to/sdk --installer downloads/n97-ngage.sisx
```

Outputs are the ordinary SIS and `build-report.json`, plus the separate X7
custom-ROM SIS and report. Each build executes the softkey adapter on an ARM
CPU to verify both ABIs, fallback behavior, saved registers and stack.

See [validation](validation/README.md) for fixtures and acceptance history and
[适配总结](docs/adaptation-summary.zh-CN.md) for the implementation summary.
Diagnostic releases remain available through Git history.
Upstream Nokia/game material is fetched separately and is not covered by this
repository's MIT license. No ROM, game or game license is included in the artifact.
