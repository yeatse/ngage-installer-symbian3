// Native compatibility implementation of Nokia's public Display Posting API.
#include "Posting_Surface.h"
#include "Posting_Surface_Factory.h"
#include <bitdev.h>
#include <bitstd.h>
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

enum class Pixel { kInvalid, k444, k555, k565, k888, kx888 };

// The patched NGI backbuffer passes formats without a posting equivalent as the
// NGI format value tagged with KPostPlatformDependent.
Pixel PixelOf(TUint format) {
    switch (format) {
    case CPostingSurface::ERgb16bit565Le: return Pixel::k565;
    case CPostingSurface::ERgb24bit888Le: return Pixel::k888;
    case CPostingSurface::ERgb32bit888Le: return Pixel::kx888;
    case KPostPlatformDependent | 0x4010: return Pixel::k444;
    case KPostPlatformDependent | 0x8010: return Pixel::k555;
    case KPostPlatformDependent | 0x10010: return Pixel::k565;
    case KPostPlatformDependent | 0x20018: return Pixel::k888;
    case KPostPlatformDependent | 0x40020: return Pixel::kx888;
    default: return Pixel::kInvalid;
    }
}
TInt BytesOf(Pixel pixel) { return pixel == Pixel::k888 ? 3 : pixel == Pixel::kx888 ? 4 : 2; }

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
    Pixel pixel_ = Pixel::kInvalid;
    TPostingParams destination_;
    bool connected_ = false;
    bool fbs_connected_ = false;
    TInt x_map_[640];
public:
    ~Surface() override {
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
        return PixelOf(format) != Pixel::kInvalid;
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
        Release(); ReleaseDirect();
        source_ = source.iSourceImageSize; pixel_ = PixelOf(source.iPostingFormat);
        buffer_.CreateL(source_, BytesOf(pixel_));
        User::LeaveIfError(SetPostingParameters(destination));
        // Matches CDirectScreenAccess: the DSA buffer follows the current app orientation.
        direct_ = CFbsScreenDevice::NewL(screen_->GetScreenNumber(), screen_->DisplayMode());
        direct_->SetAutoUpdate(EFalse);
        TPixelsAndRotation rotation;
        screen_->GetDefaultScreenSizeAndRotation(rotation);
        direct_->SetDeviceOrientation(static_cast<TDeviceOrientation>(1 << rotation.iRotation));
        User::LeaveIfError(direct_->CreateContext(gc_));
        User::LeaveIfError(bitmap_.Create(direct_->SizeInPixels(), EColor16MU));
        bitmap_.BeginDataAccess();
        auto* pixels = reinterpret_cast<TUint32*>(bitmap_.DataAddress());
        const TInt count = bitmap_.DataStride() / 4 * bitmap_.SizeInPixels().iHeight;
        for (TInt i = 0; i < count; ++i) pixels[i] = 0xff000000u;
        bitmap_.EndDataAccess();
    }
    void Destroy(TRequestStatus& status) override {
        Destroy(); TRequestStatus* request = &status; User::RequestComplete(request, KErrNone);
    }
    void Destroy() override { Release(); }
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
        if (!direct_ || !gc_) return KErrNotReady;

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
        const TInt in_bytes = BytesOf(pixel_);
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
                if (pixel_ == Pixel::k444) {
                    const TUint value = pixel[0] | (pixel[1] << 8);
                    b = (value & 15) * 17; g = ((value >> 4) & 15) * 17; r = ((value >> 8) & 15) * 17;
                } else if (pixel_ == Pixel::k555) {
                    const TUint value = pixel[0] | (pixel[1] << 8);
                    b = value & 31; b = (b << 3) | (b >> 2);
                    g = (value >> 5) & 31; g = (g << 3) | (g >> 2);
                    r = (value >> 10) & 31; r = (r << 3) | (r >> 2);
                } else if (in_bytes == 2) {
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
        if (params.iMirror || params.iRotation != ENoRotation) return KErrNotSupported;
        destination_ = params; return KErrNone;
    }
    void Release() { buffer_.Close(); bitmap_.Reset(); }
    void ReleaseDirect() { delete gc_; gc_ = nullptr; delete direct_; direct_ = nullptr; }
    TInt SetClipRegion(const TRegion& region) override {
        clip_.Copy(region);
        clip_set_ = true;
        return clip_.CheckError() ? KErrNoMemory : KErrNone;
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
