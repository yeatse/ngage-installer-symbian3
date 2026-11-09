# Building from source

This page is for developers. For phone installation instructions, see the
[README](../README.md). For implementation details, see the
[adaptation summary](adaptation-summary.md).

## GitHub Actions

The [Build unsigned Symbian installer](https://github.com/yeatse/ngage-installer-symbian3/actions/workflows/build.yml)
workflow uses [symbian-platform](https://github.com/hpnkv/symbian-platform) 0.3.0
to compile the ARM components from source and produce unsigned SIS packages.
Outputs are uploaded as the `ngage-installer-symbian3-unsigned` artifact.
Pushing a `v*` tag, or running the workflow manually with a release tag, also publishes `ngage-installer-symbian3.sis` as a GitHub release.

Large inputs are cached individually by SHA-256 and verified after every restore.
A cache hit makes no request to the source server. Regular builds do not download
ROMs or games. Input manifests are in `validation/inputs.json`,
`validation/sdk.json` and `validation/hacked-x7.json`.

## Local Linux build

Python 3.11 or newer is required. Download and verify the SDK specified in
`validation/sdk.json`, extract it to a separate directory, then run:

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements-build.txt
.venv/bin/python tools/fetch_inputs.py downloads --only n97-ngage.sisx
.venv/bin/python tools/fetch_inputs.py downloads --manifest validation/hacked-x7.json \
  --only ynstallserver-belle-fixture.exe
.venv/bin/python tools/build.py --sdk /absolute/path/to/sdk --installer downloads/n97-ngage.sisx
```

The default output directory is `artifacts/`:

| File | Purpose |
| --- | --- |
| `ngage-installer-symbian3.sis` | Ordinary hacked phones, including the 808 Belle FP2 |
| `build-report.json` | Ordinary package hash, size and adaptation details |
| `ngage-installer-x7-custom-rom.sis` | The supplied X7 custom-ROM validation environment |
| `build-report-x7-custom-rom.json` | Build report for the X7 package |

The X7 package additionally replaces `C:\sys\bin\YnstallServer.exe` to provide
installation-service compatibility for that ROM. The ordinary package uses the
phone's existing installation service. Do not use the X7 package on an 808.

## Checks and reproduction

Each build executes the source-built Thumb softkey adapter on an ARM CPU to
check both input ABIs, ordinary-key fallback, and register and stack preservation.
After building, run the full project checks with:

```sh
NGAGE_SDK=/absolute/path/to/sdk \
NGAGE_VALIDATION_INPUTS="$PWD/downloads" \
NGAGE_INSTALL_SERVICE="$PWD/downloads/ynstallserver-belle-fixture.exe" \
  .venv/bin/python -m unittest discover -s tests -v
```

See the [validation history](../validation/README.md) for runtime validation,
input sources, emulator branches and earlier issues. Passing these checks does
not establish compatibility with every game or phone model.

## Versions and upstream material

1.30(1550) removes diagnostic probes, filesystem logging and the `--diagnostic`
build option. Earlier diagnostic implementations remain available in Git history.
Old logs on the phone are not deleted automatically.

Upstream Nokia installers, ROMs, games and extracted proprietary files are not
committed to the repository and are not covered by this project's MIT license.
Build artifacts do not include ROMs, games or game licenses.
