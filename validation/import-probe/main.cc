#include <e32base.h>
#include <e32std.h>
#include <f32file.h>
#include <graphics/surfacemanager.h>

namespace {

class Log {
public:
    explicit Log(RFile &file) : iFile(file) {}
    TDes8 &Begin() { iBuffer.Zero(); return iBuffer; }
    void End() { iBuffer.Append('\n'); iFile.Write(iBuffer); iFile.Flush(); }
private:
    RFile &iFile;
    TBuf8<512> iBuffer;
};

#define LOG_LINE(log, ...) do { (log).Begin().AppendFormat(__VA_ARGS__); (log).End(); } while (0)

struct Range { TInt first, last; };

struct Library {
    const TText *name;
    // Ordinals whose code is dumped, so the function behind each can be identified.
    Range dump;
    TInt ordinals[8];
};

const Library kLibraries[] = {
    {_S("ws32.dll"), {560, 662}, {598, 600, 609, 622}},
    {_S("euser.dll"), {0, -1}, {1308, 1311, 1473, 1838, 1951}},
    {_S("bitgdi.dll"), {0, -1}, {14, 44, 52, 58, 247, 277}},
    {_S("fbscli.dll"), {0, -1}, {25, 117, 225, 249, 250}},
    {_S("gdi.dll"), {0, -1}, {169, 310, 311}},
    {_S("drtaeabi.dll"), {0, -1}, {123, 220}},
    {_S("surfacemanager.dll"), {1, 17}, {}},
    {_S("surfaceupdateclient.dll"), {1, 11}, {}},
    {_S("PostingSurfaceFactory.dll"), {0, -1}, {1}},
    {_S("ngibackbuffer.dll"), {0, -1}, {1}},
    {_S("ngidisplay.dll"), {0, -1}, {1}},
    {_S("ngigameorientation.dll"), {0, -1}, {1}},
    {_S("NgageSoundX.dll"), {0, -1}, {1}},
    {_S("ngheap.dll"), {0, -1}, {1}},
};

void Dump(Log &log, RLibrary &library, TInt ordinal) {
    const TUint address = reinterpret_cast<TUint>(library.Lookup(ordinal));
    TDes8 &line = log.Begin();
    line.AppendFormat(_L8("  #%d %08x"), ordinal, address);
    if (address) {
        const TUint8 *code = reinterpret_cast<const TUint8 *>(address & ~1u);
        for (TInt i = 0; i < 48; ++i) line.AppendFormat(i % 4 ? _L8("%02x") : _L8(" %02x"), code[i]);
    }
    log.End();
}

void ProbeLibraries(Log &log) {
    for (TUint i = 0; i < sizeof(kLibraries) / sizeof(kLibraries[0]); ++i) {
        const Library &entry = kLibraries[i];
        TPtrC name(entry.name);
        TBuf8<64> narrow;
        narrow.Copy(name);
        RLibrary library;
        const TInt r = library.Load(name);
        if (r != KErrNone) {
            LOG_LINE(log, _L8("load %S r=%d"), &narrow, r);
            continue;
        }
        TBuf8<128> path;
        path.Copy(library.FileName());
        TInt last = 0, missing = 0;
        for (TInt ordinal = 1; ordinal <= 3000; ++ordinal) {
            if (library.Lookup(ordinal)) { last = ordinal; missing = 0; }
            else if (++missing > 64) break;
        }
        LOG_LINE(log, _L8("load %S r=0 file=%S exports=%d"), &narrow, &path, last);
        for (TInt k = 0; k < 8 && entry.ordinals[k]; ++k)
            LOG_LINE(log, _L8("  #%d %08x"), entry.ordinals[k], (TUint)library.Lookup(entry.ordinals[k]));
        for (TInt ordinal = entry.dump.first; ordinal <= entry.dump.last; ++ordinal) Dump(log, library, ordinal);
        library.Close();
    }
}

template <typename Result, typename... Arguments>
Result Call(RLibrary &library, void *object, TInt ordinal, Arguments... arguments) {
    return reinterpret_cast<Result (*)(void *, Arguments...)>(library.Lookup(ordinal))(object, arguments...);
}

// The surface manager half of the posting DLL's path; showing the surface needs ws32 and is left out.
void ProbeSurface(Log &log) {
    RLibrary manager;
    TInt r = manager.Load(_L("surfacemanager.dll"));
    LOG_LINE(log, _L8("surface: load manager r=%d"), r);
    if (r != KErrNone) return;
    static TUint32 driver[8];
    Call<void>(manager, driver, 14);
    r = Call<TInt>(manager, driver, 12);
    LOG_LINE(log, _L8("surface: manager open r=%d"), r);
    if (r != KErrNone) return;
    const TSize size(360, 640);
    RSurfaceManager::TSurfaceCreationAttributesBuf package;
    RSurfaceManager::TSurfaceCreationAttributes &attributes = package();
    attributes.iSize = size;
    attributes.iBuffers = 1;
    attributes.iPixelFormat = EUidPixelFormatXRGB_8888;
    attributes.iStride = size.iWidth * 4;
    attributes.iOffsetToFirstBuffer = 0;
    attributes.iAlignment = 4;
    attributes.iContiguous = EFalse;
    attributes.iCacheAttrib = RSurfaceManager::ECached;
    attributes.iOffsetBetweenBuffers = 0;
    attributes.iSurfaceHints = nullptr;
    attributes.iHintCount = 0;
    attributes.iMappable = ETrue;
    TSurfaceId id = TSurfaceId::CreateNullId();
    r = Call<TInt, const TDesC8 *, TSurfaceId *>(manager, driver, 5, &package, &id);
    LOG_LINE(log, _L8("surface: create r=%d id=%08x %08x %08x %08x"), r, id.iInternal[0], id.iInternal[1],
             id.iInternal[2], id.iInternal[3]);
    if (r != KErrNone) return;
    RSurfaceManager::TInfoBuf info;
    r = Call<TInt, const TSurfaceId *, TDes8 *>(manager, driver, 3, &id, &info);
    LOG_LINE(log, _L8("surface: info r=%d size=%dx%d buffers=%d format=%08x stride=%d"), r, info().iSize.iWidth,
             info().iSize.iHeight, info().iBuffers, info().iPixelFormat, info().iStride);
    RChunk chunk;
    r = Call<TInt, const TSurfaceId *, RChunk *>(manager, driver, 1, &id, &chunk);
    LOG_LINE(log, _L8("surface: map r=%d"), r);
    if (r == KErrNone) {
        TInt offset = -1;
        r = Call<TInt, const TSurfaceId *, TInt, TInt *>(manager, driver, 16, &id, 0, &offset);
        LOG_LINE(log, _L8("surface: offset r=%d offset=%d base=%08x chunk=%d"), r, offset, (TUint)chunk.Base(),
                 chunk.Size());
        if (r == KErrNone) {
            TUint32 *pixel = reinterpret_cast<TUint32 *>(chunk.Base() + offset);
            *pixel = 0xff123456u;
            LOG_LINE(log, _L8("surface: wrote %08x"), *pixel);
        }
        chunk.Close();
    }
    r = Call<TInt, const TSurfaceId *>(manager, driver, 4, &id);
    Call<void>(manager, driver, 13);
    LOG_LINE(log, _L8("surface: closed r=%d"), r);
}

TInt Run() {
    RFs fs;
    TInt r = fs.Connect();
    if (r != KErrNone) return r;
    fs.MkDirAll(_L("C:\\Data\\"));
    RFile file;
    r = file.Replace(fs, _L("C:\\Data\\import_probe.txt"), EFileWrite | EFileShareExclusive);
    if (r != KErrNone) { fs.Close(); return r; }
    Log log(file);
    LOG_LINE(log, _L8("import-probe 1"));
    ProbeLibraries(log);
    ProbeSurface(log);
    LOG_LINE(log, _L8("done"));
    file.Close();
    fs.Close();
    return KErrNone;
}

}  // namespace

int main() {
    const TInt r = Run();
    User::InfoPrint(r == KErrNone ? _L("Probe: C:\\Data\\import_probe.txt") : _L("Import probe failed"));
    User::After(3000000);
    return r;
}
