// Native compatibility implementation of Nokia's public Display Posting API.
#include "Posting_Surface.h"
#include "Posting_Surface_Factory.h"
#include <bitdev.h>
#include <bitstd.h>
#include <fbs.h>
#include <w32std.h>
#include <e32property.h>
#include <graphics/surfacemanager.h>
#include <graphics/surfaceupdateclient.h>
#include <graphics/surfaceconfiguration.h>
#include "../common/touch_controls.h"
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

// The compiler zeroes Surface's members through this EABI helper, which nothing linked here provides.
extern "C" __attribute__((no_builtin("memset"))) void __aeabi_memclr4(void* data, unsigned size) {
    for (auto* byte = static_cast<volatile unsigned char*>(data); size; --size) *byte++ = 0;
}

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

// Fields of a 16-bit pixel never share a byte boundary in a way that mixes, so each byte expands alone.
TUint32 Expand(Pixel pixel, TUint value) {
    TUint b, g, r;
    if (pixel == Pixel::k444) {
        b = (value & 15) * 17; g = ((value >> 4) & 15) * 17; r = ((value >> 8) & 15) * 17;
    } else if (pixel == Pixel::k555) {
        b = value & 31; b = (b << 3) | (b >> 2);
        g = (value >> 5) & 31; g = (g << 3) | (g >> 2);
        r = (value >> 10) & 31; r = (r << 3) | (r >> 2);
    } else {
        b = value & 31; b = (b << 3) | (b >> 2);
        g = (value >> 5) & 63; g = (g << 2) | (g >> 4);
        r = (value >> 11) & 31; r = (r << 3) | (r >> 2);
    }
    return (r << 16) | (g << 8) | b;
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

// Frames go to a compositor surface shown by a window of our own. Where the device has none they go
// to the window server's DSA buffer, which it composes only inside the caller's running DSA region.
class Surface final : public CPostingSurface {
    RWsSession ws_;
    CWsScreenDevice* screen_ = nullptr;
    CFbsScreenDevice* direct_ = nullptr;
    CFbsBitGc* gc_ = nullptr;
    CFbsBitmap bitmap_;
    Buffer buffer_;
    RRegion clip_;
    bool clip_set_ = false;
    // The touch controls are blended into each frame from cache_; a window over the game would stop it drawing there.
    CFbsBitmapDevice* overlay_ = nullptr;
    CFbsBitGc* overlay_gc_ = nullptr;
    CFont* font_ = nullptr;
    RProperty touch_;
    TSize source_{0, 0};
    Pixel pixel_ = Pixel::kInvalid;
    TPostingParams destination_;
    bool connected_ = false;
    bool fbs_connected_ = false;
    // Source byte offset for each output column and source row for each output row.
    TInt x_map_[640];
    TInt y_map_[640];
    bool mapped_ = false;
    // A 16-bit source pixel is the OR of one entry for each of its bytes.
    TUint32 high_[256];
    TUint32 low_[256];
    CFbsBitmap cache_;
    TInt cache_state_ = -1;
    TUint shown_ = 0;
    TUint8 scratch_[48 * 48];
    TInt focus_ = 0;
    RSurfaceManager manager_;
    bool manager_open_ = false;
    RSurfaceUpdateSession updates_;
    TSurfaceId id_ = TSurfaceId::CreateNullId();
    RChunk shared_;
    RWindow* game_ = nullptr;
    TUint8* target_ = nullptr;
    TSize size_{0, 0};
    TInt pitch_ = 0;
public:
    ~Surface() override {
        Destroy();
        ReleaseDirect();
        clip_.Close();
        touch_.Close();
        delete screen_;
        if (fbs_connected_) RFbsSession::Disconnect();
        if (connected_) ws_.Close();
    }
    void ConstructL(TInt screen, RWindow* game) {
        if (screen != 0) User::Leave(KErrNotSupported);
        game_ = game;
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
        Release();
        delete gc_; gc_ = nullptr; delete direct_; direct_ = nullptr;
        source_ = source.iSourceImageSize; pixel_ = PixelOf(source.iPostingFormat);
        buffer_.CreateL(source_, BytesOf(pixel_));
        User::LeaveIfError(SetPostingParameters(destination));
        if (target_ && size_ != screen_->SizeInPixels()) { game_->RemoveBackgroundSurface(ETrue); CloseSurface(); }
        if (!target_ && !OpenSurface(screen_->SizeInPixels())) {
            CloseSurface();
            // Matches CDirectScreenAccess: the DSA buffer follows the current app orientation.
            direct_ = CFbsScreenDevice::NewL(screen_->GetScreenNumber(), screen_->DisplayMode());
            direct_->SetAutoUpdate(EFalse);
            TPixelsAndRotation rotation;
            screen_->GetDefaultScreenSizeAndRotation(rotation);
            direct_->SetDeviceOrientation(static_cast<TDeviceOrientation>(1 << rotation.iRotation));
            User::LeaveIfError(direct_->CreateContext(gc_));
            User::LeaveIfError(bitmap_.Create(direct_->SizeInPixels(), EColor16MU));
            size_ = bitmap_.SizeInPixels(); pitch_ = bitmap_.DataStride();
        }
        if (size_.iWidth > 640 || size_.iHeight > 640 || (pitch_ & 3)) User::Leave(KErrNotSupported);
        bitmap_.BeginDataAccess();
        auto* pixels = reinterpret_cast<TUint32*>(Frame());
        const TInt count = pitch_ / 4 * size_.iHeight;
        for (TInt i = 0; i < count; ++i) pixels[i] = 0xff000000u;
        bitmap_.EndDataAccess();
        for (TUint i = 0; i < 256; ++i) {
            low_[i] = 0xff000000u | Expand(pixel_, i);
            high_[i] = Expand(pixel_, i << 8);
        }
        User::LeaveIfError(cache_.Create(size_, EColor16MU));
        overlay_ = CFbsBitmapDevice::NewL(&cache_);
        User::LeaveIfError(overlay_->CreateContext(overlay_gc_));
        const Ngage::TouchLayout layout(size_);
        User::LeaveIfError(overlay_->GetNearestFontInPixels(font_, TFontSpec(_L("Nokia Sans S60"), layout.unit / 2)));
    }
    TUint8* Frame() { return target_ ? target_ : reinterpret_cast<TUint8*>(bitmap_.DataAddress()); }
    // One mapped XRGB buffer of the screen's size, shown as the background of the game's window.
    bool OpenSurface(TSize size) {
        if (!game_ || !game_->WsHandle()) return false;
        if (manager_.Open() != KErrNone) return false;
        manager_open_ = true;
        RSurfaceManager::TSurfaceCreationAttributesBuf package;
        RSurfaceManager::TSurfaceCreationAttributes& attributes = package();
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
        TInt offset = 0;
        if (manager_.CreateSurface(package, id_) != KErrNone) { id_ = TSurfaceId::CreateNullId(); return false; }
        if (manager_.MapSurface(id_, shared_) != KErrNone || manager_.GetBufferOffset(id_, 0, offset) != KErrNone)
            return false;
        if (updates_.Connect() != KErrNone) return false;
        TSurfaceConfiguration configuration;
        configuration.SetSurfaceId(id_);
        if (game_->SetBackgroundSurface(configuration, ETrue) != KErrNone) return false;
        target_ = shared_.Base() + offset; size_ = size; pitch_ = size.iWidth * 4;
        return true;
    }
    void CloseSurface() {
        target_ = nullptr;
        updates_.Close();
        shared_.Close();
        if (manager_open_) {
            if (!id_.IsNull()) manager_.CloseSurface(id_);
            manager_.Close(); manager_open_ = false;
        }
        id_ = TSurfaceId::CreateNullId();
    }
    enum { kToggle = Ngage::kTouchButtons, kMode, kItems };
    static TRect ItemRect(const Ngage::TouchLayout& layout, TInt item) {
        return item == kToggle ? layout.toggle : item == kMode ? layout.mode : layout.buttons[item];
    }
    static bool ItemShown(TInt item, TInt state) {
        if (!(state & Ngage::kTouchActive)) return false;
        if (item == kToggle) return true;
        if (state & Ngage::kTouchHidden) return false;
        return item == kMode || Ngage::TouchButtonShown(item, state);
    }
    void DrawItem(const TRect& bounds, TInt item, TInt state) {
        TRect area = bounds;
        area.Shrink(1, 1);
        const bool pressed = item < Ngage::kTouchButtons && (state & (1 << item));
        overlay_gc_->SetPenStyle(CGraphicsContext::ENullPen);
        overlay_gc_->SetBrushColor(pressed ? TRgb(230, 130, 20, 180) : TRgb(24, 32, 40, 140));
        overlay_gc_->DrawRect(area);
        overlay_gc_->SetPenStyle(CGraphicsContext::ESolidPen);
        overlay_gc_->SetPenColor(TRgb(255, 255, 255, 240));
        if (item == kToggle) {
            const TPoint center = bounds.Center();
            overlay_gc_->SetPenSize(TSize(2, 2));
            overlay_gc_->DrawLine(center - TPoint(6, 0), center + TPoint(7, 0));
            if (state & Ngage::kTouchHidden) overlay_gc_->DrawLine(center - TPoint(0, 6), center + TPoint(0, 7));
            overlay_gc_->SetPenSize(TSize(1, 1));
            return;
        }
        // The switch names the keys it brings up.
        const TPtrC text = item != kMode ? Ngage::TouchLabel(item) :
            (state & Ngage::kTouchKeypadShown) ? TPtrC(_L("A")) : TPtrC(_L("1"));
        const TInt baseline = (bounds.Height() - font_->HeightInPixels()) / 2 + font_->AscentInPixels();
        overlay_gc_->DrawText(text, bounds, baseline, CGraphicsContext::ECenter, 0);
    }
    // Controls are drawn only when the state changes, once over white and once over black: the
    // difference gives each pixel's alpha and the black pass its premultiplied colour.
    void Render(TInt state) {
        shown_ = 0;
        if (!overlay_gc_ || !font_) return;
        const Ngage::TouchLayout layout(cache_.SizeInPixels());
        const TRect bounds(TPoint(0, 0), cache_.SizeInPixels());
        const TInt pitch = cache_.DataStride();
        overlay_gc_->SetBrushStyle(CGraphicsContext::ESolidBrush);
        overlay_gc_->UseFont(font_);
        for (TInt item = 0; item < kItems; ++item) {
            const TRect area = ItemRect(layout, item);
            TRect inside = area;
            inside.Intersection(bounds);
            const TInt width = area.Width(), height = area.Height();
            if (!ItemShown(item, state) || inside != area || width * height > static_cast<TInt>(sizeof scratch_))
                continue;
            for (TInt pass = 0; pass < 2; ++pass) {
                overlay_gc_->SetPenStyle(CGraphicsContext::ENullPen);
                overlay_gc_->SetBrushColor(pass ? TRgb(0, 0, 0) : TRgb(255, 255, 255));
                overlay_gc_->DrawRect(area);
                DrawItem(area, item, state);
                cache_.BeginDataAccess();
                auto* first = reinterpret_cast<TUint8*>(cache_.DataAddress()) + area.iTl.iY * pitch;
                TUint8* white = scratch_;
                for (TInt y = 0; y < height; ++y) {
                    auto* row = reinterpret_cast<TUint32*>(first + y * pitch) + area.iTl.iX;
                    for (TInt x = 0; x < width; ++x, ++white) {
                        const TUint colour = row[x];
                        if (!pass) { *white = colour >> 8; continue; }
                        TInt alpha = 255 - (*white - static_cast<TInt>((colour >> 8) & 255));
                        if (alpha < 0) alpha = 0;
                        if (alpha > 255) alpha = 255;
                        TUint r = (colour >> 16) & 255, g = (colour >> 8) & 255, b = colour & 255;
                        const TUint limit = alpha;
                        if (r > limit) r = limit;
                        if (g > limit) g = limit;
                        if (b > limit) b = limit;
                        row[x] = (limit << 24) | (r << 16) | (g << 8) | b;
                    }
                }
                cache_.EndDataAccess(pass == 0);
            }
            shown_ |= 1u << item;
        }
        overlay_gc_->DiscardFont();
    }
    // The picture is rewritten every frame; the border around it is not, so a control there is
    // written over black, and blacked out once when it goes away.
    void Blend(TUint8* frame, TInt frame_pitch, const TRect& picture) {
        TInt state = 0;
        if (!touch_.Handle()) {
            const TUid category = {static_cast<TInt32>(Ngage::kTouchServiceSid)};
            touch_.Attach(category, static_cast<TUint>(RProcess().Id().Id()));
        }
        if (!touch_.Handle() || touch_.Get(state) != KErrNone) state = 0;
        const bool changed = state != cache_state_;
        if (changed) { Render(state); cache_state_ = state; }
        if (!shown_ && !changed) return;
        const Ngage::TouchLayout layout(cache_.SizeInPixels());
        const TRect bounds(TPoint(0, 0), cache_.SizeInPixels());
        const auto* cache = reinterpret_cast<const TUint8*>(cache_.DataAddress());
        const TInt pitch = cache_.DataStride();
        for (TInt item = 0; item < kItems; ++item) {
            const bool shown = shown_ & (1u << item);
            if (!shown && !changed) continue;
            const TRect area = ItemRect(layout, item);
            TRect clipped = area;
            clipped.Intersection(bounds);
            if (clipped != area || area.IsEmpty()) continue;
            TRect inside(area.iBr, area.iBr);
            if (area.Intersects(picture)) { inside = area; inside.Intersection(picture); }
            for (TInt y = area.iTl.iY; y < area.iBr.iY; ++y) {
                auto* row = reinterpret_cast<TUint32*>(frame + y * frame_pitch);
                const auto* source = reinterpret_cast<const TUint32*>(cache + y * pitch);
                const bool crosses = y >= inside.iTl.iY && y < inside.iBr.iY;
                const TInt left = crosses ? inside.iTl.iX : area.iBr.iX, right = crosses ? inside.iBr.iX : area.iBr.iX;
                for (TInt x = area.iTl.iX; x < left; ++x) row[x] = shown ? source[x] | 0xff000000u : 0xff000000u;
                for (TInt x = right; x < area.iBr.iX; ++x) row[x] = shown ? source[x] | 0xff000000u : 0xff000000u;
                if (!shown) continue;
                for (TInt x = left; x < right; ++x) {
                    const TUint over = source[x], alpha = over >> 24;
                    if (!alpha) continue;
                    const TUint under = row[x], keep = 255 - alpha;
                    row[x] = 0xff000000u | ((over & 0xffffffu) + ((((under & 0xff00ffu) * keep) >> 8) & 0xff00ffu) +
                                            ((((under & 0xff00u) * keep) >> 8) & 0xff00u));
                }
            }
        }
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
        // The focused group is checked against the game only when it changes.
        const TInt group = ws_.GetFocusWindowGroup();
        if (group != focus_) {
            TThreadId focused;
            if (ws_.GetWindowGroupClientThreadId(group, focused) != KErrNone) return KErrNone;
            RThread thread;
            if (thread.Open(focused) != KErrNone) return KErrNone;
            const TBool foreground = thread.SecureId() == RProcess().SecureId();
            thread.Close();
            if (!foreground) return KErrNone;
            focus_ = group;
        }
        if (!buffer || buffer->GetSize() != source_ || !buffer->GetBuffer()) return KErrArgument;
        if (!target_ && (!direct_ || !gc_)) return KErrNotReady;

        auto* frame = Frame();
        const TSize size = size_;
        const TInt pitch = pitch_;
        if (!frame || size.iWidth <= 0 || size.iHeight <= 0) return KErrNotReady;
        if (reinterpret_cast<TUint>(frame) & 3) return KErrNotSupported;
        TRect output = destination_.iDisplayedRect;
        if (output.IsEmpty()) return KErrNone;
        if (!destination_.iScaleToRect.IsEmpty()) {
            TRect scale = destination_.iScaleToRect; scale.Move(output.iTl); output = scale;
        }
        TRect crop = destination_.iInputCrop;
        if (crop.IsEmpty()) crop = TRect(TPoint(0, 0), source_);
        crop.Intersection(TRect(TPoint(0, 0), source_));
        if (crop.IsEmpty()) return KErrArgument;
        TRect area = output; area.Intersection(TRect(TPoint(0, 0), size));
        const auto* input = static_cast<const TUint8*>(buffer->GetBuffer());
        const TInt in_bytes = BytesOf(pixel_);
        const TInt in_stride = buffer->GetStride();
        if (in_stride < source_.iWidth * in_bytes) return KErrArgument;
        if (!mapped_) {
            for (TInt x = area.iTl.iX; x < area.iBr.iX; ++x)
                x_map_[x] = (crop.iTl.iX + Divide((x - output.iTl.iX) * crop.Width(), output.Width())) * in_bytes;
            for (TInt y = area.iTl.iY; y < area.iBr.iY; ++y)
                y_map_[y] = crop.iTl.iY + Divide((y - output.iTl.iY) * crop.Height(), output.Height());
            mapped_ = true;
        }
        const bool words = in_bytes == 4 && !(reinterpret_cast<TUint>(input) & 3) && !(in_stride & 3);
        const TInt left = area.iTl.iX, right = area.iBr.iX;
        bitmap_.BeginDataAccess();
        const TUint32* previous = nullptr;
        for (TInt y = area.iTl.iY; y < area.iBr.iY; ++y) {
            auto* row = reinterpret_cast<TUint32*>(frame + y * pitch);
            // An enlarged picture repeats source rows; the converted row is copied.
            if (previous && y_map_[y] == y_map_[y - 1]) {
                Mem::Copy(row + left, previous + left, (right - left) * 4);
                continue;
            }
            previous = row;
            const TUint8* line = input + y_map_[y] * in_stride;
            if (words) {
                for (TInt x = left; x < right; ++x)
                    row[x] = *reinterpret_cast<const TUint32*>(line + x_map_[x]) | 0xff000000u;
            } else if (in_bytes == 2) {
                for (TInt x = left; x < right; ++x) {
                    const TUint8* pixel = line + x_map_[x];
                    row[x] = high_[pixel[1]] | low_[pixel[0]];
                }
            } else {
                for (TInt x = left; x < right; ++x) {
                    const TUint8* pixel = line + x_map_[x];
                    row[x] = 0xff000000u | (pixel[2] << 16) | (pixel[1] << 8) | pixel[0];
                }
            }
        }
        Blend(frame, pitch, area);
        bitmap_.EndDataAccess();
        if (target_) { updates_.SubmitUpdate(KAllScreens, id_, 0, nullptr); return KErrNone; }
        RRegion update;
        update.AddRect(TRect(TPoint(0, 0), size));
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
        destination_ = params; mapped_ = false; return KErrNone;
    }
    void Release() {
        if (font_) overlay_->ReleaseFont(font_);
        font_ = nullptr;
        delete overlay_gc_; overlay_gc_ = nullptr;
        delete overlay_; overlay_ = nullptr;
        buffer_.Close(); bitmap_.Reset(); cache_.Reset();
        cache_state_ = -1; shown_ = 0; mapped_ = false;
    }
    void ReleaseDirect() { CloseSurface(); delete gc_; gc_ = nullptr; delete direct_; direct_ = nullptr; }
    TInt SetClipRegion(const TRegion& region) override {
        clip_.Copy(region);
        clip_set_ = true;
        return clip_.CheckError() ? KErrNoMemory : KErrNone;
    }
};}

CPostingSurfaceFactory::~CPostingSurfaceFactory() = default;
// The patched ngibackbuffer.dll passes its game window where the unpatched one passes display 0.
CPostingSurface* CPostingSurfaceFactory::NewPostingSurfaceL(TInt display) {
    auto* game = static_cast<TUint>(display) > 0xffff ? reinterpret_cast<RWindow*>(display) : nullptr;
    auto* surface = new(ELeave) Surface;
    CleanupStack::PushL(surface); surface->ConstructL(0, game); CleanupStack::Pop(surface);
    return surface;
}
__attribute__((visibility("default")))
CPostingSurfaceFactory* NewSurfaceManagerFactoryL() {
    StartControls();
    return new(ELeave) CPostingSurfaceFactory;
}
