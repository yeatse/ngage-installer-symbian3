// Native compatibility implementation of Nokia's public Display Posting API.
#include "Posting_Surface.h"
#include "Posting_Surface_Factory.h"
#include <fbs.h>
#include <w32std.h>
#include "../common/touch_service.h"


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
    if (result == KErrNone) { helper.Resume(); helper.Close(); }
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

class Surface final : public CPostingSurface {
    RWsSession ws_;
    CWsScreenDevice* screen_ = nullptr;
    CFbsBitmap bitmap_;
    CWindowGc* gc_ = nullptr;
    RWindowGroup* group_ = nullptr;
    RWindow* window_ = nullptr;
    Buffer buffer_;
    TSize source_{0, 0};
    TPostingFormat format_ = EFormatInvalidValue;
    TPostingParams destination_;
    bool connected_ = false;
    bool fbs_connected_ = false;
    TInt x_map_[640];
public:
    ~Surface() override {
        Destroy();
        if (window_) window_->Close();
        if (group_) group_->Close();
        delete window_; delete group_; delete gc_; delete screen_;
        if (fbs_connected_) RFbsSession::Disconnect();
        if (connected_) ws_.Close();
    }
    void ConstructL(TInt screen) {
        if (screen != 0) User::Leave(KErrNotSupported);
        User::LeaveIfError(ws_.Connect()); connected_ = true;
        screen_ = new(ELeave) CWsScreenDevice(ws_);
        User::LeaveIfError(screen_->Construct(screen));
        screen_->SetAppScreenMode(screen_->CurrentScreenMode());
        User::LeaveIfError(RFbsSession::Connect()); fbs_connected_ = true;
        User::LeaveIfError(screen_->CreateContext(gc_));
        group_ = new(ELeave) RWindowGroup(ws_);
        User::LeaveIfError(group_->Construct(1, EFalse));
        group_->EnableReceiptOfFocus(EFalse);
        group_->SetOrdinalPosition(0, 0);
        window_ = new(ELeave) RWindow(ws_);
        User::LeaveIfError(window_->Construct(*group_, 2));
        window_->SetBackgroundColor(TRgb(0, 0, 0));
        window_->Activate();
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
        if (!FormatSupported(source.iPostingFormat)) User::Leave(KErrNotSupported);
        if (source.iSourceImageSize.iWidth <= 0 || source.iSourceImageSize.iHeight <= 0 ||
            source.iSourceImageSize.iWidth > 640 || source.iSourceImageSize.iHeight > 640)
            User::Leave(KErrArgument);
        if (source.iBufferType != EChunkBuffer && source.iBufferType != EStandardBuffer)
            User::Leave(KErrNotSupported);
        Destroy(); source_ = source.iSourceImageSize; format_ = source.iPostingFormat;
        const TInt bytes = format_ == ERgb16bit565Le ? 2 : format_ == ERgb24bit888Le ? 3 : 4;
        buffer_.CreateL(source_, bytes);
        User::LeaveIfError(SetPostingParameters(destination));
        screen_->SetAppScreenMode(screen_->CurrentScreenMode());
        const TSize size = screen_->SizeInPixels();
        User::LeaveIfError(bitmap_.Create(size, EColor16MU));
        window_->SetExtent(TPoint(0, 0), size);
        window_->SetVisible(ETrue);
        ws_.Finish();
    }
    void Destroy(TRequestStatus& status) override {
        Destroy(); TRequestStatus* request = &status; User::RequestComplete(request, KErrNone);
    }
    void Destroy() override {
        buffer_.Close(); bitmap_.Reset(); if (window_) window_->SetVisible(EFalse);
    }
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
        TThreadId focused;
        if (ws_.GetWindowGroupClientThreadId(ws_.GetFocusWindowGroup(), focused) != KErrNone)
            return KErrNone;
        RThread thread;
        if (thread.Open(focused) != KErrNone) return KErrNone;
        const TBool foreground = thread.SecureId() == RProcess().SecureId();
        thread.Close();
        if (!foreground) return KErrNone;
        if (!buffer || buffer->GetSize() != source_ || !buffer->GetBuffer()) return KErrArgument;

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
        group_->SetOrdinalPosition(0, 0);
        gc_->Activate(*window_);
        window_->Invalidate();
        window_->BeginRedraw();
        gc_->BitBlt(TPoint(0, 0), &bitmap_);
        window_->EndRedraw();
        gc_->Deactivate();
        ws_.Finish();
        return KErrNone;
    }
    TInt SetPostingParameters(const TPostingParams& params) override {
        if (params.iMirror || params.iRotation != ENoRotation) return KErrNotSupported;
        destination_ = params; return KErrNone;
    }
    TInt SetClipRegion(const TRegion&) override {
        // WS clips this surface window; the caller's DSA region excludes it.
        return KErrNotSupported;
    }
};}

CPostingSurfaceFactory::~CPostingSurfaceFactory() = default;
CPostingSurface* CPostingSurfaceFactory::NewPostingSurfaceL(TInt display) {
    auto* surface = new(ELeave) Surface;
    CleanupStack::PushL(surface); surface->ConstructL(display); CleanupStack::Pop(surface);
    return surface;
}
__attribute__((visibility("default")))
CPostingSurfaceFactory* NewSurfaceManagerFactoryL() {
    StartControls();
    return new(ELeave) CPostingSurfaceFactory;
}
