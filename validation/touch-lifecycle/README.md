# Native touch-service lifecycle checks

This validation application is not included in either installer. It exercises
the installed `C:\sys\bin\ngage_touch.exe` using ordinary Symbian processes,
mutexes, timers and process-exit notifications. It does not require a game.

Build this project with the same SDK/toolchain as `native/touch`, then package
its E32 executable with `symbian package`. Before deployment, use
`tools/component_identity.py`'s `patch_image` and `replace_image` helpers to set
UID3/SID 0xe0000998 and capabilities 0x1804 (PowerMgmt, AllFiles, SwEvent).
PowerMgmt is used only by the validation application to terminate test owners;
the shipped touch service retains its existing capabilities.
Install the validation SIS in an isolated X7 profile containing the current
helper and launch **Touch lifecycle**. Results are written to
`E:\Data\touch-lifecycle.txt`. The application's exit reason is its failure count.

The fixture checks:

- Missing owner arguments cause the helper to exit.
- A helper remains alive with its owner even when neither has foreground focus.
- A second helper for the same owner exits without replacing the first.
- Helpers for different owners coexist.
- Ending one owner also ends its helper while another owner's helper survives.
- A normal owner return also ends its helper.

The `--owner` and `--short-owner` modes are internal fixture processes. No probes
or log-writing code from this application are linked into the shipped components.

The final 1551 fixture passed all 17 checks on Linux Release EKA2L1 with the
`fix/global-mutex-uniqueness` branch. That emulator correction makes duplicate
named mutex creation return AlreadyExists, as expected by native singleton users.
