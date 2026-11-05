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
        RFile hash;
        TInt r = hash.Open(fs, hashPath, EFileRead | EFileShareReadersOnly);
        if (r != KErrNone) {
            LOG_LINE(log, _L8("hash %S r=%d"), &file8, r);
            continue;
        }
        TBuf8<64> bytes;
        r = hash.Read(bytes);
        hash.Close();
        TBuf8<130> hex;
        for (TInt i = 0; i < bytes.Length(); ++i)
            hex.AppendFormat(_L8("%02x"), bytes[i]);
        LOG_LINE(log, _L8("hash %S r=%d %S"), &file8, r, &hex);
    }
}

TInt Run() {
    RFs fs;
    TInt r = fs.Connect();
    if (r != KErrNone) return r;
    fs.MkDirAll(_L("C:\\Data\\"));
    RFile file;
    r = file.Replace(fs, _L("C:\\Data\\rg_probe.txt"), EFileWrite | EFileShareExclusive);
    if (r != KErrNone) {
        fs.Close();
        return r;
    }
    Log log(file);
    LOG_LINE(log, _L8("rg-load-probe 1"));
    ProbeEuser(log);
    for (TUint i = 0; i < sizeof(kLibraries) / sizeof(kLibraries[0]); ++i)
        ProbeLibrary(log, kLibraries[i]);
    ProbeFiles(log, fs);
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
