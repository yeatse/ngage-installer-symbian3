# Reset Generation load probe

Diagnostic app, not part of the phone SIS. Reset Generation creates its
`ngi::CFileManagerS` by `dlopen`ing `ngifileaccess.dll` (redirected by the
game's `RG_dl.dll` to `RG_fileaccess.dll`, resolving ordinal 1) and
`ngifilemanagerfactory_2000AFC4.dll` (ordinals 1-6). If either step fails,
`mt::file::FileManagerImplNGI` dereferences its uninitialised `+0xb8` member
(exe link address `0x9cd46`) and the game exits with KERN-EXEC 3.

The probe runs with the game's capability set (`0xff5bc`), loads each library of
that chain and its dependencies with `RLibrary::Load`, resolves the ordinals the
game uses, and checks euser's old/new `UserSvr::DllTls` exports. It also records
`\sys\bin` entries and `C:\sys\hash` digests for the game binaries where the
filesystem allows it. Results are appended to `C:\Data\rg_probe.txt`, one timestamped
block per run.

It also records the activation-server state that the game's crack depends on.
`ResetGenCrack.dll` starts `AS_2000AFC4.exe` with `P` on the tenth
`CContent::NewL`. That process renames `activationengineserver.exe` (C: then E:)
to `.bak`, moves its `C:\sys\hash` digest to `.bak`, and writes the patched server
and digest stored in `C:\Private\2000AFC4\as.dat`. The seventieth call starts it
with `R`, which restores the originals. None of these file operations are
checked. The probe logs drive attributes, both server files and
digests, related processes, and loader results for `activationengineserver.exe`
and `AS_2000AFC4.exe`. It creates each process without resuming it and then kills it.

Build with `symbian-platform` 0.3.0 as for the other probes, then patch the
executable capabilities to `0xff5bc` before packaging. Install the SIS, open
"RG load probe" from the menu, then read the log.

On the X7 emulator profile every library loads and every ordinal resolves. The
808 (game on E:) gave the same load, ordinal and digest results, so the failing
step is inside `RG_fileaccess.dll` ordinal 1 rather than `dlopen`.

The SIS installs `RG_fileaccess.dll` conditionally: devices with
`z:\system\install\series60v3.1.sis` (S60 3.1/3.2, e.g. 5320) get a build that
imports euser 2176/2177 (`UserSvr::DllTls(TInt, TInt)` and `DllSetTls(TInt, TInt, TAny*)`),
like the original client's `ngifileaccess.dll`. Every other device, including
Symbian^3/Belle, gets a build importing euser 1232/1237 (`UserSvr::DllTls(TInt)`
and `DllSetTls(TInt, TAny*)`). The two builds otherwise contain the same code.
Each build uses one TLS API consistently, so this alone is not a defect.
