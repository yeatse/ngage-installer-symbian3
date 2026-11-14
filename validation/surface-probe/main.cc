#include <e32base.h>
#include <e32std.h>
#include <f32file.h>
#include <w32std.h>
#include <fbs.h>
#include <bitdev.h>
#include <bitstd.h>
#include <graphics/surfacemanager.h>
#include <graphics/surfaceupdateclient.h>
#include <graphics/surfaceconfiguration.h>
#include "Posting_Surface.h"
#include "Posting_Surface_Factory.h"

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

const TText *const kLibraries[] = {
    _S("surfacemanager.dll"), _S("surfaceupdateclient.dll"), _S("PostingSurfaceFactory.dll"),
    _S("ngibackbuffer.dll"), _S("ngidisplay.dll"), _S("ngigameorientation.dll"), _S("runtime.dll"),
    _S("NgageSoundX.dll"), _S("ngheap.dll"),
};

void ProbeLibraries(Log &log) {
    for (TUint i = 0; i < sizeof(kLibraries) / sizeof(kLibraries[0]); ++i) {
        TPtrC name(kLibraries[i]);
        TBuf8<64> narrow;
        narrow.Copy(name);
        RLibrary library;
        TInt r = library.Load(name);
        if (r != KErrNone) {
            LOG_LINE(log, _L8("load %S r=%d"), &narrow, r);
            continue;
        }
        TBuf8<128> path;
        path.Copy(library.FileName());
        TInt last = 0;
        for (TInt ordinal = 1; ordinal <= 40 && library.Lookup(ordinal); ++ordinal) last = ordinal;
        LOG_LINE(log, _L8("load %S r=0 file=%S first=%08x exports>=%d"), &narrow, &path, (TUint)library.Lookup(1), last);
        library.Close();
    }
}

template <typename Result, typename... Arguments>
Result Call(RLibrary &library, void *object, TInt ordinal, Arguments... arguments) {
    return reinterpret_cast<Result (*)(void *, Arguments...)>(library.Lookup(ordinal))(object, arguments...);
}

// The posting DLL's surface path, one logged step at a time.
void ProbeSurface(Log &log, RWindow &window, TSize size) {
    RLibrary manager, update;
    TInt r = manager.Load(_L("surfacemanager.dll"));
    LOG_LINE(log, _L8("surface: load manager r=%d"), r);
    if (r != KErrNone) return;
    r = update.Load(_L("surfaceupdateclient.dll"));
    LOG_LINE(log, _L8("surface: load update r=%d"), r);
    if (r != KErrNone) return;
    static TUint32 driver[8], session[16];
    Call<void>(manager, driver, 14);
    r = Call<TInt>(manager, driver, 12);
    LOG_LINE(log, _L8("surface: manager open r=%d"), r);
    if (r != KErrNone) return;
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
    LOG_LINE(log, _L8("surface: create %dx%d r=%d id=%08x %08x %08x %08x"), size.iWidth, size.iHeight, r,
             id.iInternal[0], id.iInternal[1], id.iInternal[2], id.iInternal[3]);
    if (r != KErrNone) return;
    RSurfaceManager::TInfoBuf info;
    r = Call<TInt, const TSurfaceId *, TDes8 *>(manager, driver, 3, &id, &info);
    LOG_LINE(log, _L8("surface: info r=%d size=%dx%d buffers=%d format=%08x stride=%d contiguous=%d mappable=%d"), r,
             info().iSize.iWidth, info().iSize.iHeight, info().iBuffers, info().iPixelFormat, info().iStride,
             info().iContiguous, info().iMappable);
    RChunk chunk;
    r = Call<TInt, const TSurfaceId *, RChunk *>(manager, driver, 1, &id, &chunk);
    LOG_LINE(log, _L8("surface: map r=%d"), r);
    if (r != KErrNone) return;
    TInt offset = -1;
    r = Call<TInt, const TSurfaceId *, TInt, TInt *>(manager, driver, 16, &id, 0, &offset);
    LOG_LINE(log, _L8("surface: offset r=%d offset=%d base=%08x chunk=%d readable=%d writable=%d"), r, offset,
             (TUint)chunk.Base(), chunk.Size(), chunk.IsReadable(), chunk.IsWritable());
    if (r != KErrNone) return;
    const TInt stride = info().iStride > 0 ? info().iStride : size.iWidth * 4;
    TUint8 *pixels = chunk.Base() + offset;
    LOG_LINE(log, _L8("surface: first pixel before=%08x"), *reinterpret_cast<TUint32 *>(pixels));
    for (TInt y = 0; y < size.iHeight; ++y) {
        TUint32 *row = reinterpret_cast<TUint32 *>(pixels + y * stride);
        for (TInt x = 0; x < size.iWidth; ++x)
            row[x] = 0xff000000u | (x * 3 / size.iWidth == 0 ? 0xff0000u : x * 3 / size.iWidth == 1 ? 0x00ff00u : 0x0000ffu);
    }
    LOG_LINE(log, _L8("surface: filled"));
    Call<void>(update, session, 8);
    r = Call<TInt, TInt>(update, session, 7, KDefaultMessageSlot);
    LOG_LINE(log, _L8("surface: update connect r=%d"), r);
    if (r != KErrNone) return;
    TSurfaceConfiguration configuration;
    configuration.SetSurfaceId(id);
    LOG_LINE(log, _L8("surface: configuration size=%d"), configuration.Size());
    r = window.SetBackgroundSurface(configuration, ETrue);
    LOG_LINE(log, _L8("surface: set background r=%d"), r);
    if (r != KErrNone) return;
    r = Call<TInt, TInt, const TSurfaceId *, TInt, const TRegion *>(update, session, 1, KAllScreens, &id, 0, nullptr);
    LOG_LINE(log, _L8("surface: submit r=%d"), r);
    User::After(4000000);
    window.RemoveBackgroundSurface(ETrue);
    LOG_LINE(log, _L8("surface: removed"));
    Call<void>(update, session, 6);
    chunk.Close();
    r = Call<TInt, const TSurfaceId *>(manager, driver, 4, &id);
    Call<void>(manager, driver, 13);
    LOG_LINE(log, _L8("surface: closed r=%d"), r);
}

// The installed posting DLL, driven the way ngibackbuffer.dll drives it.
// The font and bitmap steps of the posting DLL's InitializeL; a leave shows as the last line logged.
void ProbeBitmapL(Log &log, TSize size, TDisplayMode mode) {
    TInt r = RFbsSession::Connect();
    LOG_LINE(log, _L8("bitmap: fbs connect r=%d"), r);
    CFbsBitmap bitmap;
    r = bitmap.Create(size, mode);
    LOG_LINE(log, _L8("bitmap: create %dx%d mode=%d r=%d stride=%d"), size.iWidth, size.iHeight, mode, r, bitmap.DataStride());
    CFbsBitmap copy;
    r = copy.Duplicate(bitmap.Handle());
    LOG_LINE(log, _L8("bitmap: handle=%08x mode=%d compressed=%d duplicate r=%d mode=%d address=%08x"), bitmap.Handle(),
             bitmap.DisplayMode(), bitmap.IsCompressedInRAM(), r, copy.DisplayMode(), (TUint)copy.DataAddress());
    copy.Reset();
    CFbsBitmapDevice *device = CFbsBitmapDevice::NewL(&bitmap);
    LOG_LINE(log, _L8("bitmap: device=%08x"), (TUint)device);
    CFbsBitGc *gc = nullptr;
    r = device->CreateContext(gc);
    LOG_LINE(log, _L8("bitmap: context r=%d"), r);
    CFont *font = nullptr;
    r = device->GetNearestFontInPixels(font, TFontSpec(_L("Nokia Sans S60"), 20));
    LOG_LINE(log, _L8("bitmap: font r=%d height=%d"), r, font ? font->HeightInPixels() : 0);
    if (font) device->ReleaseFont(font);
    delete gc;
    delete device;
    bitmap.Reset();
    RFbsSession::Disconnect();
}

void ProbePostingL(Log &log, RWindow &window, TSize size, TBool hand_window) {
    RLibrary library;
    TInt r = library.Load(_L("PostingSurfaceFactory.dll"));
    LOG_LINE(log, _L8("posting: load r=%d window=%d"), r, hand_window);
    if (r != KErrNone) return;
    auto *factory = reinterpret_cast<CPostingSurfaceFactory *(*)()>(library.Lookup(1))();
    LOG_LINE(log, _L8("posting: factory=%08x"), (TUint)factory);
    CPostingSurface *surface = nullptr;
    surface = factory->NewPostingSurfaceL(hand_window ? reinterpret_cast<TInt>(&window) : 0);
    LOG_LINE(log, _L8("posting: new surface=%08x"), (TUint)surface);
    CPostingSurface::TPostingSourceParams source;
    source.iPostingBuffering = CPostingSurface::ESingleBuffering;
    source.iBufferType = CPostingSurface::EStandardBuffer;
    source.iPostingUsage = 0;
    source.iSourceImageSize = TSize(320, 240);
    source.iPostingFormat = CPostingSurface::ERgb16bit565Le;
    source.iPixelAspectRatioNum = 1;
    source.iPixelAspectRatioDenom = 1;
    source.iContentCopyRestricted = EFalse;
    CPostingSurface::TPostingParams destination;
    destination.iDisplayedRect = TRect(size);
    destination.iScaleToRect = TRect(size);
    destination.iInputCrop = TRect(0, 0, 320, 240);
    destination.iMirror = EFalse;
    destination.iRotation = CPostingSurface::ENoRotation;
    surface->InitializeL(source, destination);
    LOG_LINE(log, _L8("posting: initialized"));
    {
        for (TInt frame = 0; frame < 4; ++frame) {
            CPostingSurface::TPostingBuff *buffer = surface->NextBuffer();
            auto *pixels = buffer ? static_cast<TUint16 *>(buffer->GetBuffer()) : nullptr;
            LOG_LINE(log, _L8("posting: frame %d buffer=%08x pixels=%08x stride=%d"), frame, (TUint)buffer,
                     (TUint)pixels, buffer ? buffer->GetStride() : 0);
            if (!pixels) break;
            const TInt stride = buffer->GetStride() / 2;
            for (TInt y = 0; y < 240; ++y)
                for (TInt x = 0; x < 320; ++x)
                    pixels[y * stride + x] = ((x / 40 + y / 40 + frame) & 1) ? 0xffe0 : 0x001f;
            r = surface->PostBuffer(buffer);
            LOG_LINE(log, _L8("posting: post r=%d"), r);
            User::After(1500000);
        }
    }
    surface->Destroy();
    LOG_LINE(log, _L8("posting: destroyed"));
    delete surface;
    delete factory;
    library.Close();
    LOG_LINE(log, _L8("posting: unloaded"));
}

struct Screen {
    RWsSession ws;
    CWsScreenDevice *device = nullptr;
    RWindowGroup group;
    RWindow window;
    TSize size;
    void OpenL(Log &log) {
        User::LeaveIfError(ws.Connect());
        device = new (ELeave) CWsScreenDevice(ws);
        User::LeaveIfError(device->Construct(0));
        size = device->SizeInPixels();
        group = RWindowGroup(ws);
        User::LeaveIfError(group.Construct(reinterpret_cast<TUint32>(&group), ETrue));
        window = RWindow(ws);
        User::LeaveIfError(window.Construct(group, reinterpret_cast<TUint32>(&window)));
        window.SetExtent(TPoint(0, 0), size);
        window.SetBackgroundColor();
        window.Activate();
        group.SetOrdinalPosition(0);
        ws.Flush();
        LOG_LINE(log, _L8("window %dx%d mode=%d handle=%08x focus=%d group=%d"), size.iWidth, size.iHeight,
                 device->DisplayMode(), window.WsHandle(), ws.GetFocusWindowGroup(), group.Identifier());
    }
    void Close() { window.Close(); group.Close(); delete device; ws.Close(); }
};

enum { kLoad, kSurface, kBitmapScreen, kBitmapLandscape, kBitmap64K, kPostingWindow, kPostingDisplay, kStages };

struct Stage { Log *log; TInt index; };

void RunL(Log &log, TInt stage) {
    if (stage == kLoad) { ProbeLibraries(log); return; }
    Screen screen;
    screen.OpenL(log);
    switch (stage) {
    case kSurface: ProbeSurface(log, screen.window, screen.size); break;
    case kBitmapScreen: ProbeBitmapL(log, screen.size, EColor16MU); break;
    case kBitmapLandscape: ProbeBitmapL(log, TSize(640, 360), EColor16MU); break;
    case kBitmap64K: ProbeBitmapL(log, TSize(64, 64), EColor64K); break;
    case kPostingWindow: ProbePostingL(log, screen.window, screen.size, ETrue); break;
    case kPostingDisplay: ProbePostingL(log, screen.window, screen.size, EFalse); break;
    }
    screen.Close();
}

TInt Worker(TAny *argument) {
    auto *stage = static_cast<Stage *>(argument);
    CTrapCleanup *cleanup = CTrapCleanup::New();
    TRAPD(leave, RunL(*stage->log, stage->index));
    LOG_LINE(*stage->log, _L8("stage %d leave=%d"), stage->index, leave);
    delete cleanup;
    return KErrNone;
}

TInt Run() {
    RFs fs;
    TInt r = fs.Connect();
    if (r != KErrNone) return r;
    fs.ShareAuto();
    fs.MkDirAll(_L("C:\\Data\\"));
    RFile file;
    r = file.Open(fs, _L("C:\\Data\\surface_probe.txt"), EFileWrite | EFileShareExclusive);
    if (r == KErrNotFound)
        r = file.Create(fs, _L("C:\\Data\\surface_probe.txt"), EFileWrite | EFileShareExclusive);
    TInt end = 0;
    if (r == KErrNone) r = file.Seek(ESeekEnd, end);
    if (r != KErrNone) { fs.Close(); return r; }
    Log log(file);
    TTime now;
    now.HomeTime();
    TDateTime t = now.DateTime();
    LOG_LINE(log, _L8("surface-probe 2 %04d-%02d-%02d %02d:%02d:%02d"), t.Year(), t.Month() + 1, t.Day() + 1,
             t.Hour(), t.Minute(), t.Second());
    // Each stage has its own thread: a leave or panic ends only that stage and its exit reason is logged.
    for (TInt index = 0; index < kStages; ++index) {
        Stage stage = {&log, index};
        TBuf<16> name;
        name.Format(_L("probe-%d"), index);
        RThread worker;
        r = worker.Create(name, Worker, 0x10000, nullptr, &stage);
        LOG_LINE(log, _L8("stage %d create r=%d"), index, r);
        if (r != KErrNone) continue;
        TRequestStatus status;
        worker.Logon(status);
        worker.Resume();
        User::WaitForRequest(status);
        TBuf8<32> category;
        category.Copy(worker.ExitCategory());
        LOG_LINE(log, _L8("stage %d exit type=%d reason=%d category=%S"), index, worker.ExitType(), worker.ExitReason(), &category);
        worker.Close();
    }
    LOG_LINE(log, _L8("done r=%d"), r);
    file.Close();
    fs.Close();
    return r;
}

}  // namespace

int main() {
    TInt r = Run();
    User::InfoPrint(r == KErrNone ? _L("Probe: C:\\Data\\surface_probe.txt") : _L("Surface probe failed"));
    User::After(3000000);
    return r;
}
