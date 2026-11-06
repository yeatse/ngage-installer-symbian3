# Reset Generation libc stand-in (diagnostic)

Never shipped. Hooked's crack routes its file-manager factory's libc import
through `BiNPDAlibcCOTD.dll`. That shim sends `memcpy`/`memmove`/`memset`
(ordinals 233-235) to euser's exports 1953-1955 and forwards the rest to libc.
Reset Generation's factory imports libc directly and fails on the 808.
`factory.1` returns `0x64` at varying calls, while the same factory works on
the X7 emulator.

`rgc.dll` (UID3 `0xe00009a1`, version 1.0, capabilities `0xff5bc`) does the
same. Ordinals 233-235 call euser. Every other ordinal up to 397 looks up the
same ordinal in the real `libc.dll` at run time and tail-calls it. The SDK's
proxy numbering is not relied on. The test copy of
`ngifilemanagerfactory_2000AFC4.dll` has its import name
`libc{00010000}[10207369].dll` replaced with `rgc{00010000}[e00009a1].dll`. Its
header CRC and `C:\sys\hash` digest are regenerated. Its code is unchanged.

On the X7 emulator the game runs with both files in place.
