# Reset Generation RG_dl.dll diagnostic

A stand-in for the game's `RG_dl.dll`, never shipped in a package. It has the same
exports (1 `dlclose`, 3 `dlopen` with the `ngifileaccess.dll` →
`RG_fileaccess.dll` redirect, 4 `dlsym`), UID3/SID `0xfe314b2f`, version 1.0 and
capabilities `0xff5bc`. Every call is appended to `C:\Data\rg_dl.log` with the
exe link address of its caller. `dlsym` returns wrappers for
`RG_fileaccess.dll` ordinal 1 and the factory's ordinals 1-6. These log
arguments and results. Factory wrappers keep the caller's stack intact and hold
the return address on a per-thread shadow stack. The file-access object's two
vtables are replaced by copies. Copy slot `[-3]` keeps the original vtable and
`[-2]`/`[-1]` keep offset-to-top and typeinfo. Each method logs its entry and
tail-calls the original. Slots 6, 17, 20, 24 and 26 also log their result and
the buffer behind `r2`. Memory dumps are limited to the current thread's stack
and heap.

The 808 run showed the file manager created successfully. Factory ordinal 1,
called as `(3, 0xa00, ...)` after the second open of `base.pak`, returned `0x64`
there and `0x20` on the emulator. The paks are OMA DRM v2 DCFs. Only
`gameplay2.pak` is encrypted (the crack ships a decrypted copy); the rest use
NULL encryption. On the emulator, slot 17 reports `424413` for `base.pak`,
the size of the whole DCF rather than its `odda` payload. EKA2L1 stubs
`RFile::Adopt`, so its CAF path differs from the phone's.

The SDK only builds experimental UID3s, so the DLL is built as `0xe00009a0`
and its UID3, SID, capabilities and module version are rewritten afterwards.
The file has to be copied over `E:\sys\bin\RG_dl.dll` together with its SHA-1
in `C:\sys\hash\RG_dl.dll`, and both originals restored afterwards.

On the X7 emulator profile the game runs unchanged with it. Before the file
manager is constructed, the log shows factory 6 and factory 3 returning `0x20`,
then `RG_fileaccess` ordinal 1 (`0x1000301`) returning 0. Factory 1 and 5 then
run for each opened file.
