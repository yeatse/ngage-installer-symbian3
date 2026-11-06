# Reset Generation RG_dl.dll diagnostic

A stand-in for the game's `RG_dl.dll`, never shipped in a package. It has the same
exports (1 `dlclose`, 3 `dlopen` with the `ngifileaccess.dll` →
`RG_fileaccess.dll` redirect, 4 `dlsym`), UID3/SID `0xfe314b2f`, version 1.0 and
capabilities `0xff5bc`. Every call is appended to `C:\Data\rg_dl.log` with the
exe link address of its caller. `dlsym` returns wrappers for
`RG_fileaccess.dll` ordinal 1 and the factory's ordinals 1-6. These log
arguments and results. Factory wrappers keep the caller's stack intact and hold
the return address on a per-thread shadow stack.

The SDK only builds experimental UID3s, so the DLL is built as `0xe00009a0`
and its UID3, SID, capabilities and module version are rewritten afterwards.
The file has to be copied over `E:\sys\bin\RG_dl.dll` together with its SHA-1
in `C:\sys\hash\RG_dl.dll`, and both originals restored afterwards.

On the X7 emulator profile the game runs unchanged with it. Before the file
manager is constructed, the log shows factory 6 and factory 3 returning `0x20`,
then `RG_fileaccess` ordinal 1 (`0x1000301`) returning 0. Factory 1 and 5 then
run for each opened file.
