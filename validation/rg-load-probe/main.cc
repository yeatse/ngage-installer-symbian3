#include <e32std.h>
#include <f32file.h>

namespace {

struct Library {
    const TText *name;
    TInt ordinals[8];
};

// Reset Generation's file-manager chain first, then its dependencies, then controls.
const Library kLibraries[] = {
    { _S("RG_dl.dll"), { 1, 3, 4, 0 } },
    { _S("ResetGenCrack.dll"), { 10, 0 } },
    { _S("activationengineclient.dll"), { 1, 2, 3, 7, 0 } },
    { _S("runtime.dll"), { 5, 6, 7, 10, 0 } },
    { _S("DRMCommon.dll"), { 0 } },
    { _S("DrmRights.dll"), { 0 } },
    { _S("PlatformEnv.dll"), { 0 } },
    { _S("caf.dll"), { 0 } },
    { _S("RG_fileaccess.dll"), { 1, 0 } },
    { _S("ngifilemanagerfactory_2000AFC4.dll"), { 1, 2, 3, 4, 5, 6, 0 } },
    { _S("libc.dll"), { 0 } },
    { _S("libdl.dll"), { 0 } },
    { _S("libpthread.dll"), { 0 } },
    { _S("ngifileaccess.dll"), { 1, 0 } },
    { _S("ngifilemanagerfactory_2000afbc.dll"), { 1, 2, 3, 4, 5, 6, 0 } },
};

const TText *const kGameFiles[] = {
    _S("RG_dl.dll"), _S("ResetGenCrack.dll"), _S("RG_fileaccess.dll"),
    _S("ngifilemanagerfactory_2000AFC4.dll"), _S("ResetGeneration.exe"), _S("AS_2000AFC4.exe"),
};

const TInt kEuserOrdinals[] = { 1210, 1231, 1232, 1233, 1237, 2175, 2176, 2177, 2178 };

class Log {
public:
    explicit Log(RFile &file) : iFile(file) {}
    TDes8 &Begin() {
        iBuffer.Zero();
        return iBuffer;
    }
    void End() {
        iBuffer.Append('\n');
        iFile.Write(iBuffer);
        iFile.Flush();
    }
private:
    RFile &iFile;
    TBuf8<512> iBuffer;
};

#define LOG_LINE(log, ...) do { (log).Begin().AppendFormat(__VA_ARGS__); (log).End(); } while (0)

void Narrow(TDes8 &aOut, const TDesC &aIn) {
    aOut.Copy(aIn.Left(aOut.MaxLength()));
}

void ProbeEuser(Log &log) {
    RLibrary euser;
    TInt r = euser.Load(_L("euser.dll"));
    LOG_LINE(log, _L8("euser load %d"), r);
    if (r != KErrNone) return;
    for (TUint i = 0; i < sizeof(kEuserOrdinals) / sizeof(kEuserOrdinals[0]); ++i)
        LOG_LINE(log, _L8("euser ord %d addr %08x"), kEuserOrdinals[i], (TUint)euser.Lookup(kEuserOrdinals[i]));
    euser.Close();
}

void ProbeLibrary(Log &log, const Library &lib) {
    TPtrC name(lib.name);
    TBuf8<64> name8;
    Narrow(name8, name);
    RLibrary library;
    TInt r = library.Load(name);
    if (r != KErrNone) {
        LOG_LINE(log, _L8("load %S r=%d"), &name8, r);
        return;
    }
    TBuf8<128> path8;
    Narrow(path8, library.FileName());
    LOG_LINE(log, _L8("load %S r=0 file=%S"), &name8, &path8);
    for (TInt i = 0; i < 8 && lib.ordinals[i]; ++i)
        LOG_LINE(log, _L8("  ord %d addr %08x"), lib.ordinals[i], (TUint)library.Lookup(lib.ordinals[i]));
    library.Close();
}

_LIT(KBak, ".bak");

void LogHash(Log &log, RFs &fs, const TDesC &aPath) {
    TBuf8<128> path8;
    Narrow(path8, aPath);
    RFile hash;
    TInt r = hash.Open(fs, aPath, EFileRead | EFileShareReadersOnly);
    if (r != KErrNone) {
        LOG_LINE(log, _L8("hash %S r=%d"), &path8, r);
        return;
    }
    TBuf8<64> bytes;
    r = hash.Read(bytes);
    hash.Close();
    TBuf8<130> hex;
    for (TInt i = 0; i < bytes.Length(); ++i)
        hex.AppendFormat(_L8("%02x"), bytes[i]);
    LOG_LINE(log, _L8("hash %S r=%d %S"), &path8, r, &hex);
}

void ProbeFiles(Log &log, RFs &fs) {
    static const TText kDrives[] = { 'C', 'E', 'F' };
    for (TUint f = 0; f < sizeof(kGameFiles) / sizeof(kGameFiles[0]); ++f) {
        TPtrC file(kGameFiles[f]);
        TBuf8<64> file8;
        Narrow(file8, file);
        for (TUint d = 0; d < sizeof(kDrives) / sizeof(kDrives[0]); ++d) {
            TFileName path;
            path.Format(_L("%c:\\sys\\bin\\%S"), kDrives[d], &file);
            TEntry entry;
            TInt r = fs.Entry(path, entry);
            if (r != KErrNotFound && r != KErrPathNotFound && r != KErrNotReady)
                LOG_LINE(log, _L8("entry %c:%S r=%d size=%d"), kDrives[d], &file8, r, r == KErrNone ? entry.iSize : -1);
        }
        TFileName hashPath;
        hashPath.Format(_L("C:\\sys\\hash\\%S"), &file);
        LogHash(log, fs, hashPath);
    }
}

void LogEntry(Log &log, RFs &fs, const TDesC &aPath) {
    TBuf8<128> path8;
    Narrow(path8, aPath);
    TEntry entry;
    TInt r = fs.Entry(aPath, entry);
    LOG_LINE(log, _L8("entry %S r=%d size=%d"), &path8, r, r == KErrNone ? entry.iSize : -1);
}

void ProbeProcesses(Log &log, const TDesC &aPattern) {
    TBuf8<64> pattern8;
    Narrow(pattern8, aPattern);
    TFindProcess find(aPattern);
    TFullName name;
    TInt count = 0;
    while (find.Next(name) == KErrNone) {
        TBuf8<128> name8;
        Narrow(name8, name);
        RProcess process;
        TInt exitType = -1;
        if (process.Open(find) == KErrNone) {
            exitType = process.ExitType();
            process.Close();
        }
        LOG_LINE(log, _L8("process %S exit-type=%d"), &name8, exitType);
        ++count;
    }
    LOG_LINE(log, _L8("processes %S count=%d"), &pattern8, count);
}

// Create without resuming: exercises the loader (capabilities, hash) only.
void ProbeCreate(Log &log, const TDesC &aName) {
    TBuf8<64> name8;
    Narrow(name8, aName);
    RProcess process;
    TInt r = process.Create(aName, KNullDesC);
    if (r != KErrNone) {
        LOG_LINE(log, _L8("create %S r=%d"), &name8, r);
        return;
    }
    TBuf8<128> path8;
    Narrow(path8, process.FileName());
    LOG_LINE(log, _L8("create %S r=0 file=%S"), &name8, &path8);
    process.Kill(KErrNone);
    process.Close();
}

void ProbeActivation(Log &log, RFs &fs) {
    static const TText kDrives[] = { 'C', 'E', 'F' };
    for (TUint d = 0; d < sizeof(kDrives) / sizeof(kDrives[0]); ++d) {
        TInt drive;
        TVolumeInfo volume;
        if (fs.CharToDrive(kDrives[d], drive) == KErrNone && fs.Volume(volume, drive) == KErrNone)
            LOG_LINE(log, _L8("drive %c type=%d drive-att=%x media-att=%x"), kDrives[d],
                     volume.iDrive.iType, volume.iDrive.iDriveAtt, volume.iDrive.iMediaAtt);
        for (TInt bak = 0; bak < 2; ++bak) {
            TFileName path;
            path.Format(_L("%c:\\sys\\bin\\activationengineserver.exe%S"), kDrives[d], bak ? &KBak() : &KNullDesC());
            LogEntry(log, fs, path);
        }
    }
    LogHash(log, fs, _L("C:\\sys\\hash\\activationengineserver.exe"));
    LogHash(log, fs, _L("C:\\sys\\hash\\activationengineserver.exe.bak"));
    LogEntry(log, fs, _L("C:\\Private\\2000AFC4\\as.dat"));
    LogEntry(log, fs, _L("E:\\Private\\2000afc4\\bin\\gameplay2.pak"));
    ProbeProcesses(log, _L("activationengine*"));
    ProbeProcesses(log, _L("ResetGeneration*"));
    ProbeProcesses(log, _L("AS_2000AFC4*"));
    ProbeCreate(log, _L("activationengineserver.exe"));
    ProbeCreate(log, _L("AS_2000AFC4.exe"));
}

TInt Run() {
    RFs fs;
    TInt r = fs.Connect();
    if (r != KErrNone) return r;
    fs.MkDirAll(_L("C:\\Data\\"));
    RFile file;
    r = file.Open(fs, _L("C:\\Data\\rg_probe.txt"), EFileWrite | EFileShareExclusive);
    if (r == KErrNotFound)
        r = file.Create(fs, _L("C:\\Data\\rg_probe.txt"), EFileWrite | EFileShareExclusive);
    TInt end = 0;
    if (r == KErrNone)
        r = file.Seek(ESeekEnd, end);
    if (r != KErrNone) {
        fs.Close();
        return r;
    }
    Log log(file);
    TTime now;
    now.HomeTime();
    TDateTime t = now.DateTime();
    LOG_LINE(log, _L8("rg-load-probe 2 %04d-%02d-%02d %02d:%02d:%02d"), t.Year(), t.Month() + 1, t.Day() + 1,
             t.Hour(), t.Minute(), t.Second());
    ProbeEuser(log);
    for (TUint i = 0; i < sizeof(kLibraries) / sizeof(kLibraries[0]); ++i)
        ProbeLibrary(log, kLibraries[i]);
    ProbeFiles(log, fs);
    ProbeActivation(log, fs);
    LOG_LINE(log, _L8("done"));
    file.Close();
    fs.Close();
    return KErrNone;
}

}  // namespace

int main() {
    TInt r = Run();
    User::InfoPrint(r == KErrNone ? _L("RG probe: C:\\Data\\rg_probe.txt") : _L("RG probe failed"));
    User::After(3000000);
    return r;
}
