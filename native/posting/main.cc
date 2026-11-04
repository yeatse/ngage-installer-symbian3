// Native compatibility implementation of Nokia's public Display Posting API.
#include "Posting_Surface.h"
#include "Posting_Surface_Factory.h"
#include <bitdev.h>
#include <bitstd.h>
#include <fbs.h>
#include <w32std.h>
#include "../common/touch_service.h"
#include "../common/trace.h"


// CBase has an empty destructor. A local definition avoids a data import for
// its vtable, which the SDK's DLL converter cannot currently represent.
CBase::~CBase() {}
void operator delete(void* pointer) noexcept { User::Free(pointer); }
void operator delete(void* pointer, unsigned) noexcept { User::Free(pointer); }
extern "C" void __cxa_pure_virtual() { User::Invariant(); }

__attribute__((visibility("default")))
CPostingSurface::TPostingBuff::TPostingBuff()
    : iType(EBufferTypeInvalid), iBuffer(nullptr), iSize(0, 0), iStride(0) {}
__attribute__((visibility("default")))
CPostingSurface::TBufferType CPostingSurface::TPostingBuff::GetType() { return iType; }
__attribute__((visibility("default")))
TAny* CPostingSurface::TPostingBuff::GetBuffer() { return iBuffer; }
__attribute__((visibility("default")))
TSize CPostingSurface::TPostingBuff::GetSize() { return iSize; }
__attribute__((visibility("default")))
TUint CPostingSurface::TPostingBuff::GetStride() { return iStride; }
__attribute__((visibility("default")))
CPostingSurface::TPostingBuffExt::TPostingBuffExt() { iType = EStandardBuffer; }
__attribute__((visibility("default")))
void CPostingSurface::TPostingBuffExt::SetBuffer(TAny* p) { iBuffer = p; }
__attribute__((visibility("default")))
void CPostingSurface::TPostingBuffExt::SetSize(TSize s) { iSize = s; }
__attribute__((visibility("default")))
void CPostingSurface::TPostingBuffExt::SetStride(TUint s) { iStride = s; }
__attribute__((visibility("default")))
CPostingSurface::TPostingBufferChunk::TPostingBufferChunk() : iOffsetInChunk(0) { iType = EChunkBuffer; }
__attribute__((visibility("default")))
RChunk CPostingSurface::TPostingBufferChunk::GetChunk() { return iChunk; }
__attribute__((visibility("default")))
TUint CPostingSurface::TPostingBufferChunk::GetOffsetInChunk() { return iOffsetInChunk; }

namespace {
void StartControls() {
    RMutex existing;
    if (existing.OpenGlobal(Ngage::TouchServiceMutex(RProcess().Id())) == KErrNone) { existing.Close(); return; }
    RProcess helper;
    TBuf<32> command;
    command.AppendNum(RProcess().Id().Id());
    TInt result = helper.Create(_L("C:\\sys\\bin\\ngage_touch.exe"), command);
    Ngage::Trace("posting", "controls.create", result);
    if (result == KErrNone) { helper.Resume(); helper.Close(); }
}
// Logs export 1 of every library this process has loaded so a crash PC can be
// mapped to a module offline. Re-loading an already-loaded library shares it.
void LogModules(const char* stage) {
    TFullName pattern(RProcess().FullName());
    pattern.Append(_L("::*"));
    TFindLibrary find(pattern);
    TFullName name;
    TInt count = 0;
    while (find.Next(name) == KErrNone && count < 120) {
        ++count;
        TInt start = name.LocateReverse(':') + 1;
        TFileName file(name.Mid(start));
        TInt cut = file.Locate('{'); if (cut >= 0) file.SetLength(cut);
        cut = file.Locate('['); if (cut >= 0) file.SetLength(cut);
        if (file.Locate('.') < 0) file.Append(_L(".dll"));
        RLibrary library;
        const TInt loaded = library.Load(file);
        TInt code = 0, data = 0;
        TInt export1 = 0;
        if (loaded == KErrNone) {
            export1 = reinterpret_cast<TInt>(library.Lookup(1));
            library.GetRamSizes(code, data);
            library.Close();
        }
        Ngage::Trace("modules", stage, loaded, export1, code, &name);
    }
    TModuleMemoryInfo info;
    if (RProcess().GetMemoryInfo(info) == KErrNone)
        Ngage::Trace("modules", "exe.code.base.size", static_cast<TInt>(info.iCodeBase), static_cast<TInt>(info.iCodeSize), count);
    const TFileName image = RProcess().FileName();
    Ngage::Trace("modules", "exe.image", static_cast<TInt>(RProcess().SecureId().iId), 0, 0, &image);
}
void StartWatcher() {
    RMutex existing;
    if (existing.OpenGlobal(_L("NgageCrashWatch")) == KErrNone) { existing.Close(); return; }
    RProcess watcher;
    const TInt result = watcher.Create(_L("C:\\sys\\bin\\ngage_touch.exe"), KNullDesC);
    Ngage::Trace("posting", "watcher.create", result);
    if (result == KErrNone) { watcher.Resume(); watcher.Close(); }
}
TUint Divide(TUint numerator, TUint denominator) {
    TUint result = 0, remainder = 0;
    for (TInt bit = 31; bit >= 0; --bit) {
        remainder = (remainder << 1) | ((numerator >> bit) & 1);
        if (remainder >= denominator) {
            remainder -= denominator; result |= 1u << bit;
        }
    }
    return result;
}

class Buffer final : public CPostingSurface::TPostingBufferChunk {
public:
    void CreateL(TSize size, TInt bytes) {
        iSize = size;
        iStride = (size.iWidth * bytes + 3) & ~3;
        User::LeaveIfError(iChunk.CreateLocal(iStride * size.iHeight, iStride * size.iHeight));
        iBuffer = iChunk.Base();
    }
    void Close() { iChunk.Close(); iBuffer = nullptr; }
};

// Frames go to the window server's DSA buffer, which it composes only inside the
// caller's running DSA region. A window of our own would empty that region.
class Surface final : public CPostingSurface {
    RWsSession ws_;
    CWsScreenDevice* screen_ = nullptr;
    CFbsScreenDevice* direct_ = nullptr;
    CFbsBitGc* gc_ = nullptr;
    CFbsBitmap bitmap_;
    Buffer buffer_;
    RRegion clip_;
    bool clip_set_ = false;
    TSize source_{0, 0};
    TPostingFormat format_ = EFormatInvalidValue;
    TPostingParams destination_;
    bool connected_ = false;
    bool fbs_connected_ = false;
    TInt x_map_[640];
    TUint frames_ = 0;
    TUint skips_[8] = {};
    void Skip(TInt reason, TInt detail = 0) {
        const TUint n = ++skips_[reason];
        if (n <= 5 || (n & 255) == 0) Ngage::Trace("posting", "post.skip.reason.n", reason, n, detail);
    }
public:
    ~Surface() override {
        Ngage::Trace("posting", "surface.destruct", frames_);
        Destroy();
        ReleaseDirect();
        clip_.Close();
        delete screen_;
        if (fbs_connected_) RFbsSession::Disconnect();
        if (connected_) ws_.Close();
    }
    void ConstructL(TInt screen) {
        if (screen != 0) User::Leave(KErrNotSupported);
        User::LeaveIfError(ws_.Connect()); connected_ = true;
        screen_ = new(ELeave) CWsScreenDevice(ws_);
        User::LeaveIfError(screen_->Construct(screen));
        User::LeaveIfError(RFbsSession::Connect()); fbs_connected_ = true;
    }
    TBool FormatSupported(TPostingFormat format) override {
        return format == ERgb16bit565Le || format == ERgb24bit888Le || format == ERgb32bit888Le;
    }
    void GetCapabilities(TPostingCapab& caps) override {
        caps = TPostingCapab(); caps.iMaxPixelSize = TSize(640, 640);
        caps.iMaxSourceSize = 640 * 640 * 4;
        caps.iSupportsScaling = ETrue;
        caps.iSupportedBufferTypes = EChunkBuffer | EStandardBuffer;
        caps.iSupportedPostingBuffering = ESingleBuffering | EDoubleBuffering;
    }
    void InitializeL(const TPostingSourceParams& source, const TPostingParams& destination) override {
        Ngage::Trace("posting", "initialize.source", source.iSourceImageSize.iWidth, source.iSourceImageSize.iHeight, source.iPostingFormat);
        Ngage::Trace("posting", "initialize.buffering", source.iBufferType, source.iPostingBuffering, source.iPostingUsage);
        Ngage::Trace("posting", "initialize.dest.rect", destination.iDisplayedRect.iTl.iX, destination.iDisplayedRect.iTl.iY, destination.iDisplayedRect.Width());
        Ngage::Trace("posting", "initialize.dest.size.rot", destination.iDisplayedRect.Height(), destination.iRotation, destination.iMirror);
        Ngage::Trace("posting", "initialize.dest.scale", destination.iScaleToRect.Width(), destination.iScaleToRect.Height(), destination.iInputCrop.Width());
        if (!FormatSupported(source.iPostingFormat)) User::Leave(KErrNotSupported);
        if (source.iSourceImageSize.iWidth <= 0 || source.iSourceImageSize.iHeight <= 0 ||
            source.iSourceImageSize.iWidth > 640 || source.iSourceImageSize.iHeight > 640)
            User::Leave(KErrArgument);
        if (source.iBufferType != EChunkBuffer && source.iBufferType != EStandardBuffer)
            User::Leave(KErrNotSupported);
        Release(); ReleaseDirect();
        source_ = source.iSourceImageSize; format_ = source.iPostingFormat;
        const TInt bytes = format_ == ERgb16bit565Le ? 2 : format_ == ERgb24bit888Le ? 3 : 4;
        buffer_.CreateL(source_, bytes);
        User::LeaveIfError(SetPostingParameters(destination));
        // Matches CDirectScreenAccess: the DSA buffer follows the current app orientation.
        Ngage::Trace("posting", "ws.size.mode.dispmode", screen_->SizeInPixels().iWidth, screen_->SizeInPixels().iHeight, screen_->DisplayMode());
        Ngage::Trace("posting", "direct.new.enter", screen_->CurrentScreenMode());
        direct_ = CFbsScreenDevice::NewL(screen_->GetScreenNumber(), screen_->DisplayMode());
        Ngage::Trace("posting", "direct.new.done", reinterpret_cast<TInt>(direct_));
        direct_->SetAutoUpdate(EFalse);
        TPixelsAndRotation rotation;
        screen_->GetDefaultScreenSizeAndRotation(rotation);
        const TBool oriented = direct_->SetDeviceOrientation(static_cast<TDeviceOrientation>(1 << rotation.iRotation));
        Ngage::Trace("posting", "direct.rotation.ok.avail", rotation.iRotation, oriented, direct_->DeviceOrientationsAvailable());
        Ngage::Trace("posting", "direct.size.orient", direct_->SizeInPixels().iWidth, direct_->SizeInPixels().iHeight, direct_->DeviceOrientation());
        Ngage::Trace("posting", "default.size", rotation.iPixelSize.iWidth, rotation.iPixelSize.iHeight);
        User::LeaveIfError(direct_->CreateContext(gc_));
        User::LeaveIfError(bitmap_.Create(direct_->SizeInPixels(), EColor16MU));
        bitmap_.BeginDataAccess();
        auto* pixels = reinterpret_cast<TUint32*>(bitmap_.DataAddress());
        const TInt count = bitmap_.DataStride() / 4 * bitmap_.SizeInPixels().iHeight;
        for (TInt i = 0; i < count; ++i) pixels[i] = 0xff000000u;
        bitmap_.EndDataAccess();
        {
            const TInt group = ws_.GetFocusWindowGroup();
            TBuf<96> name;
            ws_.GetWindowGroupNameFromIdentifier(group, name);
            for (TInt i = 0; i < name.Length(); ++i) if (name[i] == 0) name[i] = '|';
            Ngage::Trace("posting", "focus.group", group, ws_.GetWindowGroupOrdinalPriority(group), 0, &name);
        }
        Ngage::Trace("posting", "initialize.done", bitmap_.SizeInPixels().iWidth, bitmap_.SizeInPixels().iHeight, reinterpret_cast<TInt>(buffer_.GetBuffer()));
        LogModules("initialize");
    }
    void Destroy(TRequestStatus& status) override {
        Destroy(); TRequestStatus* request = &status; User::RequestComplete(request, KErrNone);
    }
    void Destroy() override { Ngage::Trace("posting", "surface.destroy", frames_); Release(); }
    TInt WaitForNextBuffer(TRequestStatus& status) override {
        status = KRequestPending;
        TRequestStatus* request = &status; User::RequestComplete(request, KErrNone);
        return KErrNone;
    }
    TPostingBuff* NextBuffer() override {
        return &buffer_;
    }
    TInt CancelBuffer() override { return KErrNone; }
    TInt PostBuffer(TPostingBuff* buffer) override {
        ++frames_;
        if (frames_ <= 3 || (frames_ & 255) == 0)
            Ngage::Trace("posting", "post", frames_, reinterpret_cast<TInt>(buffer), buffer ? reinterpret_cast<TInt>(buffer->GetBuffer()) : 0);
        if (frames_ == 1) LogModules("first.post");
        TThreadId focused;
        if (ws_.GetWindowGroupClientThreadId(ws_.GetFocusWindowGroup(), focused) != KErrNone) {
            Skip(0); return KErrNone;
        }
        RThread thread;
        if (thread.Open(focused) != KErrNone) { Skip(1); return KErrNone; }
        const TBool foreground = thread.SecureId() == RProcess().SecureId();
        const TInt focused_sid = thread.SecureId().iId;
        thread.Close();
        if (!foreground) { Skip(2, focused_sid); return KErrNone; }
        if (!buffer || buffer->GetSize() != source_ || !buffer->GetBuffer()) {
            Skip(3, buffer ? buffer->GetSize().iWidth : -1); return KErrArgument;
        }
        if (!direct_ || !gc_) { Skip(4); return KErrNotReady; }

        TAcceleratedBitmapInfo bitmap;
        bitmap.iAddress = reinterpret_cast<TUint8*>(bitmap_.DataAddress());
        bitmap.iSize = bitmap_.SizeInPixels();
        bitmap.iLinePitch = bitmap_.DataStride();
        bitmap.iDisplayMode = bitmap_.DisplayMode();
        if (!bitmap.iAddress || bitmap.iSize.iWidth <= 0 || bitmap.iSize.iHeight <= 0) return KErrNotReady;
        const TInt out_bytes = bitmap.iDisplayMode == EColor64K ? 2 : bitmap.iDisplayMode == EColor16M ? 3 :
            (bitmap.iDisplayMode == EColor16MU || bitmap.iDisplayMode == EColor16MA || bitmap.iDisplayMode == EColor16MAP) ? 4 : 0;
        if (!out_bytes) return KErrNotSupported;
        TRect output = destination_.iDisplayedRect;
        if (output.IsEmpty()) return KErrNone;
        if (!destination_.iScaleToRect.IsEmpty()) {
            TRect scale = destination_.iScaleToRect; scale.Move(output.iTl); output = scale;
        }
        TRect crop = destination_.iInputCrop;
        if (crop.IsEmpty()) crop = TRect(TPoint(0, 0), source_);
        crop.Intersection(TRect(TPoint(0, 0), source_));
        if (crop.IsEmpty() || bitmap.iSize.iWidth > 640) return KErrArgument;
        TRect visible = output; visible.Intersection(TRect(TPoint(0, 0), bitmap.iSize));
        const auto* input = static_cast<const TUint8*>(buffer->GetBuffer());
        const TInt in_bytes = format_ == ERgb16bit565Le ? 2 : format_ == ERgb24bit888Le ? 3 : 4;
        const TInt in_stride = buffer->GetStride();
        if (in_stride < source_.iWidth * in_bytes) return KErrArgument;
        const bool fast32 = in_bytes == 4 && out_bytes == 4 &&
            !(reinterpret_cast<TUint>(input) & 3) && !(in_stride & 3) &&
            !(reinterpret_cast<TUint>(bitmap.iAddress) & 3) && !(bitmap.iLinePitch & 3);
        for (TInt x = visible.iTl.iX; x < visible.iBr.iX; ++x)
            x_map_[x] = crop.iTl.iX + Divide((x - output.iTl.iX) * crop.Width(), output.Width());
        const TRect area = visible;
        bitmap_.BeginDataAccess();
        for (TInt y = area.iTl.iY; y < area.iBr.iY; ++y) {
            const TInt sy = crop.iTl.iY + Divide((y - output.iTl.iY) * crop.Height(), output.Height());
            auto* row = bitmap.iAddress + y * bitmap.iLinePitch;
            for (TInt x = area.iTl.iX; x < area.iBr.iX; ++x) {
                const TInt sx = x_map_[x];
                const auto* pixel = input + sy * in_stride + sx * in_bytes;
                if (fast32) {
                    reinterpret_cast<TUint32*>(row)[x] = *reinterpret_cast<const TUint32*>(pixel) | 0xff000000u;
                    continue;
                }
                TUint b, g, r;
                if (in_bytes == 2) {
                    const TUint value = pixel[0] | (pixel[1] << 8);
                    b = value & 31; b = (b << 3) | (b >> 2);
                    g = (value >> 5) & 63; g = (g << 2) | (g >> 4);
                    r = (value >> 11) & 31; r = (r << 3) | (r >> 2);
                } else { b = pixel[0]; g = pixel[1]; r = pixel[2]; }
                auto* destination = row + x * out_bytes;
                if (out_bytes == 2) {
                    const TUint value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                    destination[0] = value; destination[1] = value >> 8;
                } else {
                    destination[0] = b; destination[1] = g; destination[2] = r;
                    if (out_bytes == 4) destination[3] = 255;
                }
            }
        }
        bitmap_.EndDataAccess();
        RRegion update;
        update.AddRect(TRect(TPoint(0, 0), bitmap.iSize));
        if (clip_set_) update.Intersect(clip_);
        if (frames_ <= 3 || (frames_ & 255) == 0) {
            const TRect bounds = update.BoundingRect();
            Ngage::Trace("posting", "post.update.count.set", update.Count(), clip_set_, update.CheckError());
            Ngage::Trace("posting", "post.update.tl", bounds.iTl.iX, bounds.iTl.iY);
            Ngage::Trace("posting", "post.update.br", bounds.iBr.iX, bounds.iBr.iY);
        }
        if (!update.CheckError() && !update.IsEmpty()) {
            gc_->SetClippingRegion(update);
            gc_->BitBlt(TPoint(0, 0), &bitmap_);
            gc_->CancelClippingRegion();
            direct_->Update(update);
        }
        update.Close();
        return KErrNone;
    }
    TInt SetPostingParameters(const TPostingParams& params) override {
        Ngage::Trace("posting", "parameters.rect", params.iDisplayedRect.Width(), params.iDisplayedRect.Height(), params.iRotation);
        if (params.iMirror || params.iRotation != ENoRotation) return KErrNotSupported;
        destination_ = params; return KErrNone;
    }
    void Release() { buffer_.Close(); bitmap_.Reset(); }
    void ReleaseDirect() { delete gc_; gc_ = nullptr; delete direct_; direct_ = nullptr; }
    TInt SetClipRegion(const TRegion& region) override {
        const TRect bounds = region.BoundingRect();
        Ngage::Trace("posting", "clip.count.frames", region.Count(), frames_);
        Ngage::Trace("posting", "clip.tl", bounds.iTl.iX, bounds.iTl.iY);
        Ngage::Trace("posting", "clip.br", bounds.iBr.iX, bounds.iBr.iY);
        for (TInt i = 0; i < region.Count() && i < 24; ++i) {
            const TRect& r = region.RectangleList()[i];
            Ngage::Trace("posting", "clip.rect.tl", i, r.iTl.iX, r.iTl.iY);
            Ngage::Trace("posting", "clip.rect.br", i, r.iBr.iX, r.iBr.iY);
        }
        clip_.Copy(region);
        clip_set_ = true;
        return clip_.CheckError() ? KErrNoMemory : KErrNone;
    }
};}

CPostingSurfaceFactory::~CPostingSurfaceFactory() = default;
CPostingSurface* CPostingSurfaceFactory::NewPostingSurfaceL(TInt display) {
    Ngage::Trace("posting", "surface.new", display);
    auto* surface = new(ELeave) Surface;
    CleanupStack::PushL(surface); surface->ConstructL(display); CleanupStack::Pop(surface);
    return surface;
}
__attribute__((visibility("default")))
CPostingSurfaceFactory* NewSurfaceManagerFactoryL() {
    Ngage::Trace("posting", "factory.entry", 1556, reinterpret_cast<TInt>(&NewSurfaceManagerFactoryL));
    LogModules("factory");
    StartWatcher();
    StartControls();
    return new(ELeave) CPostingSurfaceFactory;
}
