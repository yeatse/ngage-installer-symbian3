#include <e32base.h>
#include <e32std.h>
#include <f32file.h>
#include <w32std.h>
#include <videoplayer.h>
#include <graphics/surfacemanager.h>
#include <graphics/surfaceupdateclient.h>
#include <graphics/surfaceconfiguration.h>
#include "Posting_Surface.h"
#include "Posting_Surface_Factory.h"

namespace {

class Log {
public:
    explicit Log(RFile &file) : iFile(file) {}
    TDes8 &Begin() {
        iBuffer.Zero();
        iBuffer.AppendFormat(_L8("%u "), User::NTickCount());
        return iBuffer;
    }
    void End() { iBuffer.Append('\n'); iFile.Write(iBuffer); iFile.Flush(); }
private:
    RFile &iFile;
    TBuf8<512> iBuffer;
};

#define LOG_LINE(log, ...) do { (log).Begin().AppendFormat(__VA_ARGS__); (log).End(); } while (0)

TFileName gVideo;

// The intro video is packed inside the game, so the user copies an MP4 to one of these.
TBool FindVideo(RFs &fs, Log &log) {
    const TText *const names[] = {
        _S("C:\\Data\\video_ngage.mp4"), _S("E:\\video_ngage.mp4"), _S("F:\\video_ngage.mp4"),
        _S("E:\\Data\\video_ngage.mp4"), _S("F:\\Data\\video_ngage.mp4"),
        // Readable with Open4All.
        _S("C:\\Private\\2001092B\\video_ngage.MP4"), _S("E:\\Private\\2001092B\\video_ngage.MP4"),
        _S("F:\\Private\\2001092B\\video_ngage.MP4"),
    };
    TEntry entry;
    for (TUint i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (fs.Entry(TPtrC(names[i]), entry) == KErrNone) { gVideo = TPtrC(names[i]); break; }
    }
    if (!gVideo.Length()) {
        const TText *const folders[] = {_S("C:\\Data\\Videos\\"), _S("E:\\Videos\\"), _S("F:\\Videos\\")};
        for (TUint i = 0; i < sizeof(folders) / sizeof(folders[0]) && !gVideo.Length(); ++i) {
            CDir *dir = nullptr;
            TFileName spec;
            spec = TPtrC(folders[i]);
            spec.Append(_L("*.mp4"));
            if (fs.GetDir(spec, KEntryAttNormal, ESortByName, dir) == KErrNone && dir->Count() > 0) {
                gVideo = TPtrC(folders[i]);
                gVideo.Append((*dir)[0].iName);
            }
            delete dir;
        }
    }
    TBuf8<256> narrow;
    narrow.Copy(gVideo);
    LOG_LINE(log, _L8("video %S"), &narrow);
    return gVideo.Length() > 0;
}

class Dsa : public CBase, public MDirectScreenAccess {
public:
    explicit Dsa(Log &log) : iLog(log) {}
    ~Dsa() { delete iDsa; }
    void StartL(RWsSession &ws, CWsScreenDevice &device, RWindowBase &window) {
        iDsa = CDirectScreenAccess::NewL(ws, device, window, *this);
        TRAPD(r, iDsa->StartL());
        LOG_LINE(iLog, _L8("dsa: start r=%d rects=%d"), r, r == KErrNone ? iDsa->DrawingRegion()->Count() : -1);
        User::LeaveIfError(r);
    }
    void AbortNow(RDirectScreenAccess::TTerminationReasons reason) override {
        LOG_LINE(iLog, _L8("dsa: abort reason=%d"), reason);
    }
    void Restart(RDirectScreenAccess::TTerminationReasons reason) override {
        TRAPD(r, iDsa->StartL());
        LOG_LINE(iLog, _L8("dsa: restart reason=%d r=%d"), reason, r);
    }
private:
    Log &iLog;
    CDirectScreenAccess *iDsa = nullptr;
};

template <typename Result, typename... Arguments>
Result Call(RLibrary &library, void *object, TInt ordinal, Arguments... arguments) {
    return reinterpret_cast<Result (*)(void *, Arguments...)>(library.Lookup(ordinal))(object, arguments...);
}

// A compositor surface as the window's background, set the way the posting DLL sets it.
class Surface {
public:
    explicit Surface(Log &log) : iLog(log) {}
    ~Surface() { Remove(); }
    TInt Set(RWindow &window, TSize size) {
        TInt r = iManager.Load(_L("surfacemanager.dll"));
        if (r == KErrNone) r = iUpdate.Load(_L("surfaceupdateclient.dll"));
        if (r == KErrNone) r = iWs32.Load(_L("ws32.dll"));
        if (r != KErrNone) { LOG_LINE(iLog, _L8("surface: load r=%d"), r); return r; }
        Call<void>(iManager, iDriver, 14);
        r = Call<TInt>(iManager, iDriver, 12);
        if (r != KErrNone) { LOG_LINE(iLog, _L8("surface: open r=%d"), r); return r; }
        iOpen = ETrue;
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
        r = Call<TInt, const TDesC8 *, TSurfaceId *>(iManager, iDriver, 5, &package, &iId);
        if (r != KErrNone) { LOG_LINE(iLog, _L8("surface: create r=%d"), r); return r; }
        RSurfaceManager::TInfoBuf info;
        Call<TInt, const TSurfaceId *, TDes8 *>(iManager, iDriver, 3, &iId, &info);
        r = Call<TInt, const TSurfaceId *, RChunk *>(iManager, iDriver, 1, &iId, &iChunk);
        TInt offset = 0;
        if (r == KErrNone) r = Call<TInt, const TSurfaceId *, TInt, TInt *>(iManager, iDriver, 16, &iId, 0, &offset);
        if (r != KErrNone) { LOG_LINE(iLog, _L8("surface: map r=%d"), r); return r; }
        const TInt stride = info().iStride > 0 ? info().iStride : size.iWidth * 4;
        for (TInt y = 0; y < size.iHeight; ++y) {
            TUint32 *row = reinterpret_cast<TUint32 *>(iChunk.Base() + offset + y * stride);
            for (TInt x = 0; x < size.iWidth; ++x) row[x] = 0xff0000c0u;
        }
        Call<void>(iUpdate, iSession, 8);
        r = Call<TInt, TInt>(iUpdate, iSession, 7, KDefaultMessageSlot);
        if (r != KErrNone) { LOG_LINE(iLog, _L8("surface: update connect r=%d"), r); return r; }
        iConnected = ETrue;
        TSurfaceConfiguration configuration;
        configuration.SetSurfaceId(iId);
        // By ordinal: the 808's loader refuses an image that imports ws32 ordinal 598.
        r = Call<TInt, const TSurfaceConfiguration *, TInt>(iWs32, &window, 598, &configuration, ETrue);
        LOG_LINE(iLog, _L8("surface: set background r=%d"), r);
        if (r != KErrNone) return r;
        iWindow = &window;
        r = Call<TInt, TInt, const TSurfaceId *, TInt, const TRegion *>(iUpdate, iSession, 1, KAllScreens, &iId, 0, nullptr);
        LOG_LINE(iLog, _L8("surface: submit r=%d"), r);
        return KErrNone;
    }
    void Remove() {
        if (iWindow) {
            Call<void, TInt>(iWs32, iWindow, 600, ETrue);
            LOG_LINE(iLog, _L8("surface: removed"));
            iWindow = nullptr;
        }
        if (iConnected) { Call<void>(iUpdate, iSession, 6); iConnected = EFalse; }
        iChunk.Close();
        if (iOpen) {
            if (!iId.IsNull()) Call<TInt, const TSurfaceId *>(iManager, iDriver, 4, &iId);
            Call<void>(iManager, iDriver, 13);
            iOpen = EFalse;
        }
        iWs32.Close();
        iUpdate.Close();
        iManager.Close();
    }
private:
    Log &iLog;
    RLibrary iManager, iUpdate, iWs32;
    TUint32 iDriver[8] = {}, iSession[16] = {};
    TSurfaceId iId = TSurfaceId::CreateNullId();
    RChunk iChunk;
    RWindow *iWindow = nullptr;
    TBool iOpen = EFalse, iConnected = EFalse;
};

// The installed posting DLL with the game's window, after one frame, as Asphalt 4 has it at its intro.
class Posting {
public:
    explicit Posting(Log &log) : iLog(log) {}
    ~Posting() { Destroy(); delete iSurface; delete iFactory; iLibrary.Close(); }
    void StartL(RWindow &window, TSize size) {
        User::LeaveIfError(iLibrary.Load(_L("PostingSurfaceFactory.dll")));
        iFactory = reinterpret_cast<CPostingSurfaceFactory *(*)()>(iLibrary.Lookup(1))();
        iSurface = iFactory->NewPostingSurfaceL(reinterpret_cast<TInt>(&window));
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
        iSurface->InitializeL(source, destination);
        iInitialized = ETrue;
        CPostingSurface::TPostingBuff *buffer = iSurface->NextBuffer();
        auto *pixels = buffer ? static_cast<TUint16 *>(buffer->GetBuffer()) : nullptr;
        if (!pixels) User::Leave(KErrGeneral);
        const TInt stride = buffer->GetStride() / 2;
        for (TInt y = 0; y < 240; ++y)
            for (TInt x = 0; x < 320; ++x) pixels[y * stride + x] = 0x07e0;
        TInt r = iSurface->PostBuffer(buffer);
        LOG_LINE(iLog, _L8("posting: frame r=%d"), r);
    }
    void Destroy() {
        if (iInitialized) { iSurface->Destroy(); iInitialized = EFalse; LOG_LINE(iLog, _L8("posting: destroyed")); }
    }
private:
    Log &iLog;
    RLibrary iLibrary;
    CPostingSurfaceFactory *iFactory = nullptr;
    CPostingSurface *iSurface = nullptr;
    TBool iInitialized = EFalse;
};

class Player : public CBase, public MVideoPlayerUtilityObserver {
public:
    explicit Player(Log &log) : iLog(log) {}
    // The utility is not deleted: on the 808 deleting it after Close never returned. The thread's
    // exit closes its sessions instead.
    ~Player() { delete iTimer; }
    // Plays up to eight seconds; returns the first error of NewL, open, prepare or play.
    TInt Run(RWsSession &ws, CWsScreenDevice &device, RWindowBase &window, TSize size) {
        const TRect rect(size);
        TRAPD(r, iUtility = CVideoPlayerUtility::NewL(*this, EMdaPriorityNormal, EMdaPriorityPreferenceNone, ws,
                                                     device, window, rect, rect));
        LOG_LINE(iLog, _L8("video: new r=%d"), r);
        if (r != KErrNone) return r;
        TRAP(r, iUtility->OpenFileL(gVideo));
        LOG_LINE(iLog, _L8("video: open r=%d"), r);
        if (r != KErrNone) return r;
        iTimer = CPeriodic::NewL(CActive::EPriorityStandard);
        iTimer->Start(15000000, 15000000, TCallBack(Timeout, this));
        CActiveScheduler::Start();
        if (iPlaying) iUtility->Stop();
        iUtility->Close();
        return iError;
    }
    void MvpuoOpenComplete(TInt error) override {
        LOG_LINE(iLog, _L8("video: open complete r=%d"), error);
        if (error != KErrNone) { Finish(error); return; }
        iUtility->Prepare();
    }
    void MvpuoPrepareComplete(TInt error) override {
        TSize frame;
        TRAP_IGNORE(iUtility->VideoFrameSizeL(frame));
        LOG_LINE(iLog, _L8("video: prepare complete r=%d frame=%dx%d"), error, frame.iWidth, frame.iHeight);
        if (error != KErrNone) { Finish(error); return; }
        iUtility->Play();
        iPlaying = ETrue;
        iTimer->Cancel();
        iTimer->Start(8000000, 8000000, TCallBack(Timeout, this));
    }
    void MvpuoFrameReady(CFbsBitmap &, TInt) override {}
    void MvpuoPlayComplete(TInt error) override {
        LOG_LINE(iLog, _L8("video: play complete r=%d"), error);
        iPlaying = EFalse;
        Finish(error);
    }
    void MvpuoEvent(const TMMFEvent &event) override {
        LOG_LINE(iLog, _L8("video: event %08x r=%d"), event.iEventType.iUid, event.iErrorCode);
    }
private:
    static TInt Timeout(TAny *self) {
        auto *player = static_cast<Player *>(self);
        LOG_LINE(player->iLog, _L8("video: %s"), player->iPlaying ? "played 8 s" : "timeout");
        player->Finish(player->iPlaying ? KErrNone : KErrTimedOut);
        return 0;
    }
    void Finish(TInt error) {
        if (iDone) return;
        iDone = ETrue;
        iError = error;
        iTimer->Cancel();
        CActiveScheduler::Stop();
    }
    Log &iLog;
    CVideoPlayerUtility *iUtility = nullptr;
    CPeriodic *iTimer = nullptr;
    TBool iPlaying = EFalse, iDone = EFalse;
    TInt iError = KErrNone;
};

enum {
    kPlain,             // nothing on the window
    kDsa,               // a running DSA, as the game's ngibackbuffer
    kSurface,           // a compositor background surface only
    kDsaSurface,        // DSA started, then the surface
    kPosting,           // DSA plus the installed posting DLL after a frame: the game's state
    kPostingDestroyed,  // the same, with the posting surface destroyed before the video
    kStages
};

const TText8 *const kStageNames[] = {
    _S8("plain"), _S8("dsa"), _S8("surface"), _S8("dsa+surface"), _S8("dsa+posting"), _S8("posting destroyed"),
};

void RunL(Log &log, TInt stage) {
    RWsSession ws;
    User::LeaveIfError(ws.Connect());
    CleanupClosePushL(ws);
    CWsScreenDevice *device = new (ELeave) CWsScreenDevice(ws);
    CleanupStack::PushL(device);
    User::LeaveIfError(device->Construct(0));
    const TSize size = device->SizeInPixels();
    RWindowGroup group(ws);
    User::LeaveIfError(group.Construct(reinterpret_cast<TUint32>(&group), ETrue));
    CleanupClosePushL(group);
    RWindow window(ws);
    User::LeaveIfError(window.Construct(group, reinterpret_cast<TUint32>(&window)));
    CleanupClosePushL(window);
    window.SetExtent(TPoint(0, 0), size);
    window.SetBackgroundColor(KRgbDarkGray);
    window.Activate();
    group.SetOrdinalPosition(0);
    ws.Flush();
    LOG_LINE(log, _L8("stage %d %s window %dx%d"), stage, kStageNames[stage], size.iWidth, size.iHeight);
    User::After(500000);

    Dsa *dsa = new (ELeave) Dsa(log);
    CleanupStack::PushL(dsa);
    Surface *surface = new (ELeave) Surface(log);
    CleanupStack::PushL(surface);
    Posting *posting = new (ELeave) Posting(log);
    CleanupStack::PushL(posting);
    if (stage == kDsa || stage == kDsaSurface || stage == kPosting || stage == kPostingDestroyed)
        dsa->StartL(ws, *device, window);
    if (stage == kSurface || stage == kDsaSurface) User::LeaveIfError(surface->Set(window, size));
    if (stage == kPosting || stage == kPostingDestroyed) {
        TRAPD(r, posting->StartL(window, size));
        LOG_LINE(log, _L8("posting: start r=%d"), r);
        User::LeaveIfError(r);
        ws.Flush();
        User::After(1000000);
        if (stage == kPostingDestroyed) posting->Destroy();
    }
    ws.Flush();
    User::After(1000000);

    Player *player = new (ELeave) Player(log);
    CleanupStack::PushL(player);
    TInt r = player->Run(ws, *device, window, size);
    LOG_LINE(log, _L8("stage %d %s result=%d"), stage, kStageNames[stage], r);
    CleanupStack::PopAndDestroy(8, &ws);
    LOG_LINE(log, _L8("stage %d closed"), stage);
}

struct Stage { Log *log; TInt index; };

TInt Worker(TAny *argument) {
    auto *stage = static_cast<Stage *>(argument);
    CTrapCleanup *cleanup = CTrapCleanup::New();
    CActiveScheduler *scheduler = new CActiveScheduler;
    CActiveScheduler::Install(scheduler);
    TRAPD(leave, RunL(*stage->log, stage->index));
    LOG_LINE(*stage->log, _L8("stage %d leave=%d"), stage->index, leave);
    delete scheduler;
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
    r = file.Open(fs, _L("C:\\Data\\video_probe.txt"), EFileWrite | EFileShareAny);
    if (r == KErrNotFound) r = file.Create(fs, _L("C:\\Data\\video_probe.txt"), EFileWrite | EFileShareAny);
    TInt end = 0;
    if (r == KErrNone) r = file.Seek(ESeekEnd, end);
    if (r != KErrNone) { fs.Close(); return r; }
    Log log(file);
    TTime now;
    now.HomeTime();
    TDateTime t = now.DateTime();
    LOG_LINE(log, _L8("video-probe 2 %04d-%02d-%02d %02d:%02d:%02d"), t.Year(), t.Month() + 1, t.Day() + 1,
             t.Hour(), t.Minute(), t.Second());
    if (!FindVideo(fs, log)) {
        file.Close();
        fs.Close();
        return KErrNotFound;
    }
    // Each stage has its own thread: a leave or panic ends only that stage and its exit reason is logged.
    for (TInt index = 0; index < kStages; ++index) {
        Stage stage = {&log, index};
        TBuf<16> name;
        name.Format(_L("video-%d"), index);
        RThread worker;
        r = worker.Create(name, Worker, 0x10000, 0x10000, 0x800000, &stage);
        if (r != KErrNone) { LOG_LINE(log, _L8("stage %d create r=%d"), index, r); continue; }
        TRequestStatus status;
        worker.Logon(status);
        worker.Resume();
        // A stage that hangs is killed so the next one still runs.
        RTimer timer;
        timer.CreateLocal();
        TRequestStatus timeout;
        timer.After(timeout, 40000000);
        User::WaitForRequest(status, timeout);
        if (status == KRequestPending) {
            LOG_LINE(log, _L8("stage %d hung, killed"), index);
            worker.Kill(KErrTimedOut);
            User::WaitForRequest(status);
        } else {
            timer.Cancel();
            User::WaitForRequest(timeout);
        }
        timer.Close();
        TBuf8<32> category;
        category.Copy(worker.ExitCategory());
        LOG_LINE(log, _L8("stage %d exit type=%d reason=%d category=%S"), index, worker.ExitType(),
                 worker.ExitReason(), &category);
        worker.Close();
        User::After(1000000);
    }
    LOG_LINE(log, _L8("done"));
    file.Close();
    fs.Close();
    return KErrNone;
}

}  // namespace

int main() {
    TInt r = Run();
    User::InfoPrint(r == KErrNone ? _L("Probe: C:\\Data\\video_probe.txt")
                    : r == KErrNotFound ? _L("Copy an MP4 to C:\\Data\\video_ngage.mp4")
                                        : _L("Video probe failed"));
    User::After(3000000);
    return r;
}
