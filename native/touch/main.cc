#include <e32std.h>
#include <e32keys.h>
#include <e32hal.h>
#include <gdi.h>
#include <w32std.h>
#include <algorithm>
#include <array>
#include <memory>
#include "../common/touch_service.h"
#include "../common/trace.h"

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
    RProcess game_;
    TRequestStatus game_exit_;
    bool watching_game_ = false;
    std::array<RThread, 24> threads_;
    std::array<TUint, 24> thread_ids_{};
    int thread_count_ = 0;
    std::unique_ptr<CWsScreenDevice> screen_;
    std::unique_ptr<CWindowGc> gc_;
    RWindowGroup group_;
    RTimer timer_;
    std::array<std::unique_ptr<RWindow>, kWindowCount> windows_;
    std::array<TRect, kWindowCount> bounds_;
    std::array<int, 16> pointers_;
    std::array<int, kButtonCount> held_{};
    std::array<unsigned, kButtonCount> repeat_at_{};
    CFont* font_ = nullptr;
    TRequestStatus events_, redraws_, tick_;
    TSize size_{0, 0};
    TInt target_ = 0;
    TInt last_focus_ = -1;
    unsigned clock_ = 0;
    bool active_ = false;
    bool hidden_ = false;
    bool requests_ = false;

    bool GameHasFocus(TInt focus) {
        TThreadId id;
        if (ws_.GetWindowGroupClientThreadId(focus, id) != KErrNone) return false;
        RThread thread;
        if (thread.Open(id) != KErrNone) return false;
        RProcess process;
        const TInt result = thread.Process(process);
        thread.Close();
        if (result != KErrNone) return false;
        const bool matches = process.Id() == game_.Id();
        process.Close();
        return matches;
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

    void ScanThreads() {
        TFullName pattern(game_.FullName());
        pattern.Append(_L("::*"));
        TFindThread find(pattern);
        TFullName name;
        while (find.Next(name) == KErrNone && thread_count_ < static_cast<int>(threads_.size())) {
            RThread thread;
            if (thread.Open(find) != KErrNone) continue;
            const TUint id = static_cast<TUint>(thread.Id().Id());
            if (std::find(thread_ids_.begin(), thread_ids_.begin() + thread_count_, id) != thread_ids_.begin() + thread_count_) {
                thread.Close(); continue;
            }
            Ngage::Trace("touch", "thread.watch", static_cast<TInt>(id), thread_count_, 0, &name);
            thread_ids_[thread_count_] = id;
            threads_[thread_count_++] = thread;
        }
    }
    void ReportThread(RThread& thread, TUint id) {
        const TExitCategoryName category = thread.ExitCategory();
        Ngage::Trace("touch", "thread.exit.type.reason.id", thread.ExitType(), thread.ExitReason(), static_cast<TInt>(id), &category);
        if (thread.ExitType() == EExitPending || thread.ExitType() == EExitKill) return;
        TBuf8<128> context;
        thread.Context(context);
        const auto* regs = reinterpret_cast<const TUint32*>(context.Ptr());
        const TInt words = context.Length() / 4;
        Ngage::Trace("touch", "context.words", words, static_cast<TInt>(id));
        for (TInt i = 0; i < words; i += 3) {
            TBuf<64> line;
            for (TInt j = i; j < i + 3 && j < words; ++j) { line.Append(_L(" r")); line.AppendNum(j); line.Append('='); line.AppendNum(regs[j], EHex); }
            Ngage::Trace("touch", "context.hex", i, 0, 0, &line);
        }
    }
    void ReportExit() {
        const TExitCategoryName category = game_.ExitCategory();
        Ngage::Trace("touch", "game.exit.type.reason", game_.ExitType(), game_.ExitReason(), thread_count_, &category);
        for (int i = 0; i < thread_count_; ++i) ReportThread(threads_[i], thread_ids_[i]);
    }
    void Poll() {
        ++clock_;
        if (clock_ % 5 == 0) ScanThreads();
        const TInt focus = ws_.GetFocusWindowGroup();
        if (focus != last_focus_ || (!active_ && clock_ % 10 == 0)) {
            const bool is_game = GameHasFocus(focus);
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
    int Run(TProcessId game_id) {
        TInt result = game_.Open(game_id);
        Ngage::Trace("touch", "game.open", result, static_cast<TInt>(game_id.Id()), 1551);
        if (result != KErrNone) return result;
        {
            const TFileName image = game_.FileName();
            Ngage::Trace("touch", "game.image.sid", static_cast<TInt>(game_.SecureId().iId), 0, 0, &image);
            TModuleMemoryInfo info;
            const TInt memory = game_.GetMemoryInfo(info);
            Ngage::Trace("touch", "game.code.base.size", memory, static_cast<TInt>(info.iCodeBase), static_cast<TInt>(info.iCodeSize));
        }
        ScanThreads();
        if (game_.ExitType() != EExitPending) return KErrNone;
        game_.Logon(game_exit_);
        watching_game_ = true;
        if (game_exit_ != KRequestPending)
            return game_.ExitType() == EExitPending ? game_exit_.Int() : KErrNone;
        result = ws_.Connect();
        if (result != KErrNone) return result;
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
            if (game_exit_ != KRequestPending) { ReportExit(); return KErrNone; }
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
        if (watching_game_ && game_exit_ == KRequestPending) {
            game_.LogonCancel(game_exit_);
            User::WaitForRequest(game_exit_);
        }
        game_.Close();
        for (int i = 0; i < thread_count_; ++i) threads_[i].Close();
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
    }
};

// Diagnostic watcher: records the exit and register context of every thread in
// non-ROM processes, including crashes before the display factory starts.
struct Watched {
    RProcess process;
    TUint id = 0;
    std::array<RThread, 16> threads;
    std::array<TUint, 16> thread_ids{};
    std::array<bool, 16> reported{};
    std::array<std::array<TUint32, 18>, 16> last{};
    std::array<TUint8, 16> same{};
    std::array<TUint8, 16> logged{};
    bool sample = false;
    int thread_count = 0;
};

void ReportContext(RThread& thread, TUint id) {
    TBuf8<128> context;
    thread.Context(context);
    const auto* regs = reinterpret_cast<const TUint32*>(context.Ptr());
    const TInt words = context.Length() / 4;
    Ngage::Trace("watch", "context.words", words, static_cast<TInt>(id));
    for (TInt i = 0; i < words; i += 3) {
        TBuf<96> line;
        for (TInt j = i; j < i + 3 && j < words; ++j) {
            line.Append(_L(" r")); line.AppendNum(j); line.Append('='); line.AppendNum(regs[j], EHex);
        }
        Ngage::Trace("watch", "context", i, 0, 0, &line);
    }
    if (words < 16) return;
    const TUint pc = regs[15];
    if (pc >= 0x80000000u) for (TUint at = (pc & ~3u) - 0x20; at < (pc & ~3u) + 0x20; at += 16) {
        TBuf<96> line;
        line.AppendNum(at, EHex); line.Append(':');
        for (TUint j = 0; j < 16; j += 4) {
            line.Append(' '); line.AppendNum(*reinterpret_cast<const TUint32*>(at + j), EHex);
        }
        Ngage::Trace("watch", "pc.words", 0, 0, 0, &line);
    }
    // ROM code is mapped at the same address in every process, so the nearest
    // export below each ROM-looking register names the function it points into.
    std::array<TUint, 18> best{}; std::array<TInt, 18> best_ordinal{}; std::array<TBuf<40>, 18> best_file;
    TFindLibrary find(_L("*"));
    TFullName name;
    TInt count = 0;
    while (find.Next(name) == KErrNone && count < 400) {
        ++count;
        const TInt start = name.LocateReverse(':') + 1;
        TFileName file(name.Mid(start));
        TInt cut = file.Locate('{'); if (cut >= 0) file.SetLength(cut);
        cut = file.Locate('['); if (cut >= 0) file.SetLength(cut);
        if (file.Locate('.') < 0) file.Append(_L(".dll"));
        RLibrary library;
        if (library.Load(file) != KErrNone) continue;
        for (TInt ordinal = 1; ordinal < 5000; ++ordinal) {
            const TUint address = reinterpret_cast<TUint>(library.Lookup(ordinal)) & ~1u;
            if (!address) break;
            for (TInt r = 0; r < words && r < 18; ++r) {
                if (regs[r] < 0x80000000u || regs[r] >= 0xa0000000u) continue;
                if (address <= regs[r] && address > best[r]) { best[r] = address; best_ordinal[r] = ordinal; best_file[r] = file.Left(40); }
            }
        }
        library.Close();
    }
    for (TInt r = 0; r < words && r < 18; ++r)
        if (best[r]) Ngage::Trace("watch", "reg.export.ordinal.offset", r, best_ordinal[r],
                                  static_cast<TInt>(regs[r] - best[r]), &best_file[r]);
}

void TraceFreeRam(TInt id) {
    TMemoryInfoV1Buf info;
    if (UserHal::MemoryInfo(info) == KErrNone)
        Ngage::Trace("watch", "ram.free.total.pid", info().iFreeRamInBytes, info().iTotalRamInBytes, id);
}

int WatchAll() {
    RMutex single;
    if (single.CreateGlobal(Text(u"NgageCrashWatch")) != KErrNone) return KErrNone;
    Ngage::Trace("watch", "start", 1556);
    {
        RLibrary library;
        const TInt loaded = library.Load(_L("PostingSurfaceFactory.dll"));
        TFileName file;
        if (loaded == KErrNone) file = library.FileName();
        Ngage::Trace("watch", "posting.load", loaded, loaded == KErrNone ? reinterpret_cast<TInt>(library.Lookup(1)) : 0, 0, &file);
        if (loaded == KErrNone) library.Close();
    }
    static std::array<Watched, 24> watched;
    const TUint self = static_cast<TUint>(RProcess().Id().Id());
    // Processes running before the watcher are baseline; only later ones get slots.
    std::array<TUint, 128> baseline{};
    TInt baseline_count = 0;
    bool first = true;
    TUint poll = 0;
    for (;;) {
        TFindProcess find(_L("*"));
        TFullName name;
        while (find.Next(name) == KErrNone) {
            RProcess process;
            if (process.Open(find) != KErrNone) continue;
            const TUint id = static_cast<TUint>(process.Id().Id());
            const TFileName image = process.FileName();
            bool known = id == self || process.ExitType() != EExitPending ||
                         (image.Length() > 0 && (image[0] == 'Z' || image[0] == 'z'));
            for (auto& item : watched) if (item.id == id) known = true;
            for (TInt i = 0; i < baseline_count; ++i) if (baseline[i] == id) known = true;
            const TUint32 sid = process.SecureId().iId;
            if (first && sid != 0x2000AFC4 && sid != 0x2000AFBC) {
                if (!known && baseline_count < static_cast<TInt>(baseline.size())) baseline[baseline_count++] = id;
                process.Close(); continue;
            }
            Watched* slot = nullptr;
            if (!known) for (auto& item : watched) if (!item.id) { slot = &item; break; }
            if (!slot) {
                if (!known) {
                    Ngage::Trace("watch", "process.unwatched", static_cast<TInt>(process.SecureId().iId), static_cast<TInt>(id), 0, &image);
                    if (baseline_count < static_cast<TInt>(baseline.size())) baseline[baseline_count++] = id;
                }
                process.Close(); continue;
            }
            slot->process = process; slot->id = id; slot->thread_count = 0;
            // Diagnostic: sample Reset Generation and Hooked (control) thread contexts.
            slot->sample = sid == 0x2000AFC4 || sid == 0x2000AFBC;
            TModuleMemoryInfo info;
            const TInt memory = process.GetMemoryInfo(info);
            Ngage::Trace("watch", "process.sid", static_cast<TInt>(process.SecureId().iId), static_cast<TInt>(id), 0, &image);
            TraceFreeRam(static_cast<TInt>(id));
            Ngage::Trace("watch", "process.code.base.size", memory, static_cast<TInt>(info.iCodeBase), static_cast<TInt>(info.iCodeSize));
        }
        for (auto& item : watched) {
            if (!item.id) continue;
            TFullName pattern(item.process.FullName());
            pattern.Append(_L("::*"));
            TFindThread threads(pattern);
            while (threads.Next(name) == KErrNone && item.thread_count < static_cast<int>(item.threads.size())) {
                RThread thread;
                if (thread.Open(threads) != KErrNone) continue;
                const TUint tid = static_cast<TUint>(thread.Id().Id());
                bool seen = false;
                for (int i = 0; i < item.thread_count; ++i) if (item.thread_ids[i] == tid) seen = true;
                if (seen) { thread.Close(); continue; }
                Ngage::Trace("watch", "thread", static_cast<TInt>(item.id), static_cast<TInt>(tid), 0, &name);
                item.thread_ids[item.thread_count] = tid;
                item.reported[item.thread_count] = false;
                item.same[item.thread_count] = 0; item.logged[item.thread_count] = 0;
                item.last[item.thread_count].fill(0);
                item.threads[item.thread_count++] = thread;
            }
            if (item.sample && (poll % 5) == 0) for (int i = 0; i < item.thread_count; ++i) {
                RThread& thread = item.threads[i];
                if (thread.ExitType() != EExitPending) continue;
                TBuf8<128> context;
                thread.Context(context);
                std::array<TUint32, 18> regs{};
                Mem::Copy(regs.data(), context.Ptr(), Min(context.Length(), 72));
                if (regs == item.last[i]) {
                    if (++item.same[i] == 3) {
                        Ngage::Trace("watch", "stuck.tid", static_cast<TInt>(item.thread_ids[i]));
                        ReportContext(thread, item.thread_ids[i]);
                    }
                    continue;
                }
                item.last[i] = regs; item.same[i] = 0;
                if (item.logged[i] >= 40) continue;
                ++item.logged[i];
                TBuf<128> line;
                for (TInt r = 0; r < 16; ++r) { line.AppendNum(regs[r], EHex); line.Append(' '); }
                Ngage::Trace("watch", "sample.tid", static_cast<TInt>(item.thread_ids[i]), 0, 0, &line);
            }
            for (int i = 0; i < item.thread_count; ++i) {
                RThread& thread = item.threads[i];
                if (item.reported[i] || thread.ExitType() == EExitPending) continue;
                const TExitCategoryName category = thread.ExitCategory();
                Ngage::Trace("watch", "thread.exit.type.reason.tid", thread.ExitType(), thread.ExitReason(),
                             static_cast<TInt>(item.thread_ids[i]), &category);
                if (thread.ExitType() == EExitPanic || thread.ExitType() == EExitTerminate) {
                    ReportContext(thread, item.thread_ids[i]);
                    TraceFreeRam(static_cast<TInt>(item.id));
                }
                item.reported[i] = true;
            }
            if (item.process.ExitType() != EExitPending) {
                const TExitCategoryName category = item.process.ExitCategory();
                Ngage::Trace("watch", "process.exit.type.reason.pid", item.process.ExitType(), item.process.ExitReason(),
                             static_cast<TInt>(item.id), &category);
                for (int i = 0; i < item.thread_count; ++i) item.threads[i].Close();
                item.process.Close(); item.id = 0; item.thread_count = 0;
            }
        }
        if (first) Ngage::Trace("watch", "baseline", baseline_count);
        first = false;
        ++poll;
        User::After(200000);
    }
}
}

int main() {
    TBuf<32> command;
    if (User::CommandLineLength() > command.MaxLength()) return KErrArgument;
    User::CommandLine(command);
    if (command.Length() == 0) return WatchAll();
    TLex parser(command);
    TInt64 id = 0;
    if (parser.Val(id, EDecimal) != KErrNone || !parser.Eos() || id <= 0) return KErrArgument;
    const TProcessId game_id(id);
    RMutex single;
    const TInt created = single.CreateGlobal(Ngage::TouchServiceMutex(game_id));
    if (created == KErrAlreadyExists) return KErrNone;
    if (created != KErrNone) return created;
    TInt result = KErrNone;
    {
        Controls controls;
        result = controls.Run(game_id);
    }
    Ngage::Trace("touch", "service.exit", result);
    single.Close();
    return result;
}
