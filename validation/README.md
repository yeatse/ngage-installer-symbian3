# Validation inputs and tools

`inputs.json` pins the four externally supplied inputs by URL and SHA-256.
Downloads, ROMs, extracted proprietary files and emulator state stay outside Git.
Python 3.11 or newer is required for the host tools.

```sh
python3 tools/fetch_inputs.py downloads
NGAGE_VALIDATION_INPUTS="$PWD/downloads" python3 -m unittest discover -s tests -v
python3 tools/extract_client.py downloads/n97-ngage.sisx \
  --output build/original-client.sisx
```

The extractor reads the original installer controller to locate the embedded
client, checks package CRCs, and publishes the original client bytes. Repacking
checks retain the original signed controller and payload data. These host checks
cover package integrity; runtime validation is performed separately in EKA2L1.

Start a locally built desktop EKA2L1 with `--debug-control-port 8765`. Run
`tools/control.py` in the same network namespace as the emulator, for example:

```sh
python3 tools/control.py GET /v1/status --output artifacts/status.json
python3 tools/control.py GET /v1/screenshot --output artifacts/screen.png
```

The required runtime acceptance sequence is:

1. Install the original N97 installer on an isolated N97 ROM profile.
2. Place the unchanged `.n-gage` file in `E:\n-gage`.
3. Open Games, complete game installation there, and launch the installed game.
4. Exercise gameplay and retain screenshots, input history and emulator logs.
5. Repeat on an isolated X7 profile with the adapted unsigned installer.
6. Verify left directional controls, right A/B controls, hide/show, held keys,
   key release on hiding or app switching, and translucent rendering.

Emulator reference revision: `8ad3e14dad84db064c6efe29abf3553e26610a67`.
Native application SDK: `hpnkv/symbian-platform` release `v0.3.0`.

## GitHub Actions input caching

The reusable `.github/actions/cache-input` action restores an individual file by
its expected SHA-256, then runs the same hash verification as local builds. A
cache hit makes no request to the original server. There are no broad fallback
keys that could substitute a different version. GitHub may evict inactive caches;
a missing cache is populated from the pinned source again.

The package tooling workflow requests only `n97-ngage.sisx`. ROMs and the sample
game are reserved for emulator validation jobs. Updating one resource's digest
does not invalidate cached copies of the other resources. Local downloads are
also reused and verified; `--only FILENAME` selects a single manifest entry.

The native SDK has a separate `validation/sdk.json` manifest. Build jobs can use
the same cache action with `manifest: validation/sdk.json` and
`filename: symbian-sdk-0.3.0-linux-x86_64.tar.gz`. Runtime test jobs select ROMs
and the game individually from `validation/inputs.json`.

Cache behavior was exercised in Actions run `41217431339`: attempt 1 downloaded
and saved the pinned installer; attempt 2 restored the same key and verified the
local file without downloading. All four package checks passed on both attempts.
