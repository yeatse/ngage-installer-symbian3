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
