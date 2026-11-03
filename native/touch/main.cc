#include <e32std.h>
#include <e32keys.h>
#include <f32file.h>
#include <gdi.h>
#include <w32std.h>
#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace {
TPtrC Text(const char16_t* text) {
    return TPtrC(reinterpret_cast<const TUint16*>(text));
}

struct Binding {
    TInt scan;
    TInt code;
    const char16_t* label;
};
constexpr std::array<Binding, 9> kBindings{{
    {EStdKeyUpArrow, EKeyUpArrow, u"↑"},
    {EStdKeyLeftArrow, EKeyLeftArrow, u"←"},
    {EStdKeyDevice3, EKeyDevice3, u"OK"},
    {EStdKeyRightArrow, EKeyRightArrow, u"→"},
    {EStdKeyDownArrow, EKeyDownArrow, u"↓"},
    {'5', '5', u"A"}, {'7', '7', u"B"},
    {EStdKeyDevice0, EKeyDevice0, u"L"},
    {EStdKeyDevice1, EKeyDevice1, u"R"}
}};

static_assert(EStdKeyDevice0 == 0xa4 && EStdKeyDevice1 == 0xa5);
static_assert(EKeyDevice0 == 0xf842 && EKeyDevice1 == 0xf843);

constexpr int kButtonCount = kBindings.size();
constexpr int kToggle = kButtonCount;
constexpr int kWindowCount = kButtonCount + 1;

struct ScopedWsSession : RWsSession {
    ~ScopedWsSession() { Close(); }
};

class Controls {
    ScopedWsSession ws_;
    RFs files_;
    std::unique_ptr<CWsScreenDevice> screen_;
    std::unique_ptr<CWindowGc> gc_;
    RWindowGroup group_;
    RTimer timer_;
    std::array<std::unique_ptr<RWindow>, kWindowCount> windows_;
    std::array<TRect, kWindowCount> bounds_;
    std::array<int, 16> pointers_;
    std::array<int, kButtonCount> held_{};
    std::array<unsigned, kButtonCount> repeat_at_{};
    std::vector<TUint32> games_;
    CFont* font_ = nullptr;
    TRequestStatus events_, redraws_, tick_;
    TSize size_{0, 0};
    TInt target_ = 0;
    TInt last_focus_ = -1;
    unsigned clock_ = 0;
    bool active_ = false;
    bool hidden_ = false;
    bool requests_ = false;

    void ScanGames() {
        games_.clear();
        for (const auto* path : {u"C:\\private\\20007b39\\game*",
                                u"E:\\private\\20007b39\\game*"}) {
            CDir* entries = nullptr;
            if (files_.GetDir(Text(path), KEntryAttDir, ESortByName, entries) != KErrNone) continue;
            for (TInt i = 0; i < entries->Count(); ++i) {
                const auto& entry = (*entries)[i];
                if (!entry.IsDir() || entry.iName.Length() <= 4) continue;
                TLex number(entry.iName.Mid(4));
                TUint uid = 0;
                if (number.Val(uid, EDecimal) == KErrNone && number.Eos()) games_.push_back(uid);
            }
            delete entries;
        }
    }

    TUint32 FocusSid(TInt focus) {
        TThreadId id;
        if (ws_.GetWindowGroupClientThreadId(focus, id) != KErrNone) return 0;
        RThread thread;
        if (thread.Open(id) != KErrNone) return 0;
        const auto sid = thread.SecureId().iId;
        thread.Close();
        return sid;
    }

    void Send(int button, TInt type, TInt repeats = 0) {
        if (target_ <= 0 || button < 0 || button >= kButtonCount) return;
        TWsEvent event;
        event.SetType(type);
        event.SetTimeNow();
        event.SetHandle(0);
        TKeyEvent key{};
        key.iScanCode = kBindings[button].scan;
        key.iCode = kBindings[button].code;
        key.iRepeats = repeats;
        *event.Key() = key;
        ws_.SendEventToWindowGroup(target_, event);
    }

    void ReleaseAll() {
        for (int i = 0; i < kButtonCount; ++i) {
            if (held_[i]) Send(i, EEventKeyUp);
            held_[i] = 0;
        }
        pointers_.fill(-1);
    }

    void Visible() {
        for (int i = 0; i < kWindowCount; ++i) {
            windows_[i]->SetVisible(active_ && (i == kToggle || !hidden_));
            if (active_) windows_[i]->Invalidate();
        }
    }

    void Layout() {
        screen_->SetAppScreenMode(screen_->CurrentScreenMode());
        const auto size = screen_->SizeInPixels();
        if (size == size_) return;
        ReleaseAll();
        size_ = size;
        const int unit = std::clamp(std::min(size.iWidth, size.iHeight) / 7, 28, 48);
        const int y = (size.iHeight - 3 * unit) / 2;
        const int x = 6;
        bounds_[0] = TRect(x + unit, y, x + 2 * unit, y + unit);
        bounds_[1] = TRect(x, y + unit, x + unit, y + 2 * unit);
        bounds_[2] = TRect(x + unit, y + unit, x + 2 * unit, y + 2 * unit);
        bounds_[3] = TRect(x + 2 * unit, y + unit, x + 3 * unit, y + 2 * unit);
        bounds_[4] = TRect(x + unit, y + 2 * unit, x + 2 * unit, y + 3 * unit);
        const int right = size.iWidth - unit - 8;
        bounds_[5] = TRect(right, y + unit / 2, right + unit, y + unit * 3 / 2);
        bounds_[6] = TRect(right, y + unit * 3 / 2 + 8, right + unit, y + unit * 5 / 2 + 8);
        bounds_[7] = TRect(right, size.iHeight - unit - 8, right + unit, size.iHeight - 8);
        bounds_[8] = TRect(right, 8, right + unit, 8 + unit);
        bounds_[kToggle] = TRect(6, 8, 38, 40);
        for (int i = 0; i < kWindowCount; ++i) windows_[i]->SetExtent(bounds_[i].iTl, bounds_[i].Size());
        Visible();
    }

    void Press(int pointer, int button) {
        const int old = pointers_[pointer];
        if (old == button) return;
        if (old >= 0 && old < kButtonCount && --held_[old] == 0) {
            Send(old, EEventKeyUp);
            windows_[old]->Invalidate();
        }
        pointers_[pointer] = button;
        if (button >= 0 && button < kButtonCount && held_[button]++ == 0) {
            Send(button, EEventKeyDown);
            Send(button, EEventKey);
            repeat_at_[button] = clock_ + 4;
            windows_[button]->Invalidate();
        }
    }

    void Pointer(const TWsEvent& event) {
        const int index = event.Handle() - 2;
        if (index < 0 || index >= kWindowCount || !active_) return;
        const auto* pointer = event.Pointer();
        const int number = pointer->PointerNumber();
        if (number < 0 || number >= static_cast<int>(pointers_.size())) return;
        if (pointer->iType == TPointerEvent::EButton1Up) {
            Press(number, -1);
            return;
        }
        if (pointer->iType != TPointerEvent::EButton1Down && pointer->iType != TPointerEvent::EDrag) return;
        if (index == kToggle && pointer->iType == TPointerEvent::EButton1Down) {
            ReleaseAll();
            hidden_ = !hidden_;
            Visible();
            return;
        }
        if (hidden_) return;
        TPoint position = bounds_[index].iTl + pointer->iPosition;
        int hit = -1;
        for (int i = 0; i < kButtonCount; ++i) if (bounds_[i].Contains(position)) hit = i;
        if (pointer->iType == TPointerEvent::EButton1Down) windows_[index]->ClaimPointerGrab(number);
        Press(number, hit);
    }

    void Draw(int index, const TRect& rectangle) {
        auto& window = *windows_[index];
        gc_->Activate(window);
        window.BeginRedraw(rectangle);
        gc_->SetPenStyle(CGraphicsContext::ENullPen);
        gc_->SetBrushStyle(CGraphicsContext::ESolidBrush);
        gc_->SetBrushColor(TRgb(0, 0, 0, 0));
        gc_->Clear();
        const bool pressed = index < kButtonCount && held_[index];
        gc_->SetBrushColor(pressed ? TRgb(230, 130, 20, 180) : TRgb(24, 32, 40, 140));
        const TRect area(TPoint(1, 1), bounds_[index].Size() - TSize(2, 2));
        gc_->DrawRect(area);
        gc_->SetPenStyle(CGraphicsContext::ESolidPen);
        gc_->SetPenColor(TRgb(255, 255, 255, 240));
        if (index == kToggle) {
            const TPoint center(bounds_[index].Width() / 2, bounds_[index].Height() / 2);
            gc_->SetPenSize(TSize(2, 2));
            gc_->DrawLine(TPoint(center.iX - 6, center.iY), TPoint(center.iX + 7, center.iY));
            if (hidden_) gc_->DrawLine(TPoint(center.iX, center.iY - 6), TPoint(center.iX, center.iY + 7));
            gc_->SetPenSize(TSize(1, 1));
        } else {
            gc_->UseFont(font_);
            const TInt baseline = (area.Height() - font_->HeightInPixels()) / 2 + font_->AscentInPixels();
            gc_->DrawText(Text(kBindings[index].label), area, baseline, CGraphicsContext::ECenter, 0);
            gc_->DiscardFont();
        }
        window.EndRedraw();
        gc_->Deactivate();
    }

    void Poll() {
        ++clock_;
        const TInt focus = ws_.GetFocusWindowGroup();
        if (focus != last_focus_ || (!active_ && clock_ % 10 == 0)) {
            const auto sid = FocusSid(focus);
            if (focus != last_focus_) ScanGames();
            const bool is_game = std::find(games_.begin(), games_.end(), sid) != games_.end();
            if (focus != target_ || is_game != active_) {
                ReleaseAll();
                target_ = is_game ? focus : 0;
                active_ = is_game;
                Visible();
            }
            last_focus_ = focus;
        }
        if (active_) {
            Layout();
            for (int i = 0; i < kButtonCount; ++i) {
                if (held_[i] && clock_ >= repeat_at_[i]) Send(i, EEventKey, 1);
            }
        }
    }

public:
    Controls() { pointers_.fill(-1); }
    int Run() {
        TInt result = ws_.Connect();
        if (result != KErrNone) return result;
        if ((result = files_.Connect()) != KErrNone) return result;
        screen_ = std::make_unique<CWsScreenDevice>(ws_);
        if ((result = screen_->Construct()) != KErrNone) return result;
        gc_ = std::make_unique<CWindowGc>(screen_.get());
        if ((result = gc_->Construct()) != KErrNone) return result;
        group_ = RWindowGroup(ws_);
        if ((result = group_.Construct(1, EFalse)) != KErrNone) return result;
        group_.EnableReceiptOfFocus(EFalse);
        group_.SetOrdinalPosition(0, 500);
        if ((result = screen_->GetNearestFontInTwips(font_, TFontSpec(Text(u"Nokia Sans S60"), 240))) != KErrNone) return result;
        for (int i = 0; i < kWindowCount; ++i) {
            windows_[i] = std::make_unique<RWindow>(ws_);
            if ((result = windows_[i]->Construct(group_, i + 2)) != KErrNone) return result;
            windows_[i]->SetRequiredDisplayMode(EColor16MA);
            if ((result = windows_[i]->SetTransparencyAlphaChannel()) != KErrNone) return result;
            windows_[i]->SetBackgroundColor(TRgb(0, 0, 0, 0));
            windows_[i]->EnableAdvancedPointers();
            windows_[i]->SetVisible(EFalse);
            windows_[i]->Activate();
        }
        if ((result = timer_.CreateLocal()) != KErrNone) return result;
        Layout();
        ws_.EventReady(&events_);
        ws_.RedrawReady(&redraws_);
        timer_.After(tick_, 100000);
        requests_ = true;
        ws_.Flush();
        for (;;) {
            User::WaitForAnyRequest();
            if (events_ != KRequestPending) {
                if (events_.Int() != KErrNone) return events_.Int();
                TWsEvent event;
                ws_.GetEvent(event);
                if (event.Type() == EEventPointer) Pointer(event);
                ws_.EventReady(&events_);
            }
            if (redraws_ != KRequestPending) {
                if (redraws_.Int() != KErrNone) return redraws_.Int();
                TWsRedrawEvent redraw;
                ws_.GetRedraw(redraw);
                const int index = redraw.Handle() - 2;
                if (index >= 0 && index < kWindowCount) Draw(index, redraw.Rect());
                ws_.RedrawReady(&redraws_);
            }
            if (tick_ != KRequestPending) {
                if (tick_.Int() != KErrNone) return tick_.Int();
                Poll();
                timer_.After(tick_, 100000);
            }
            ws_.Flush();
        }
    }
    ~Controls() {
        ReleaseAll();
        if (requests_) {
            timer_.Cancel();
            ws_.EventReadyCancel();
            ws_.RedrawReadyCancel();
            if (tick_ == KRequestPending) User::WaitForRequest(tick_);
            if (events_ == KRequestPending) User::WaitForRequest(events_);
            if (redraws_ == KRequestPending) User::WaitForRequest(redraws_);
        }
        timer_.Close();
        for (auto& window : windows_) if (window) window->Close();
        group_.Close();
        if (font_) screen_->ReleaseFont(font_);
        files_.Close();
    }
};
}

int main() {
    RMutex single;
    const TInt created = single.CreateGlobal(Text(u"NgageTouchControls"));
    if (created == KErrAlreadyExists) return KErrNone;
    if (created != KErrNone) return created;
    TInt result = KErrNone;
    {
        Controls controls;
        result = controls.Run();
    }
    single.Close();
    return result;
}
