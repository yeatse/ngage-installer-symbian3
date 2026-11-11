#include <e32std.h>
#include <e32keys.h>
#include <w32std.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <e32property.h>
#include "../common/touch_controls.h"
#include "../common/touch_service.h"

namespace {
struct Binding {
    TInt scan;
    TInt code;
};
constexpr std::array<Binding, 21> kBindings{{
    {EStdKeyUpArrow, EKeyUpArrow},
    {EStdKeyLeftArrow, EKeyLeftArrow},
    {EStdKeyDevice3, EKeyDevice3},
    {EStdKeyRightArrow, EKeyRightArrow},
    {EStdKeyDownArrow, EKeyDownArrow},
    // Primary/secondary action keys of the N97 NGI input profile that Symbian^3 selects.
    {EStdKeyEnter, EKeyEnter}, {EStdKeyBackspace, EKeyBackspace},
    {EStdKeyDevice0, EKeyDevice0},
    {EStdKeyDevice1, EKeyDevice1},
    // Phone keypad keys reach the game as they do from a hardware keypad.
    {'1', '1'}, {'2', '2'}, {'3', '3'}, {'4', '4'}, {'5', '5'}, {'6', '6'},
    {'7', '7'}, {'8', '8'}, {'9', '9'}, {'*', '*'}, {'0', '0'}, {EStdKeyHash, '#'}
}};

static_assert(EStdKeyDevice0 == 0xa4 && EStdKeyDevice1 == 0xa5);
static_assert(EKeyDevice0 == 0xf842 && EKeyDevice1 == 0xf843);
static_assert(EStdKeyEnter == 0x03 && EStdKeyBackspace == 0x01);

constexpr int kButtonCount = kBindings.size();
static_assert(kButtonCount == Ngage::kTouchButtons);
constexpr unsigned Bit(int button) { return 1u << button; }

struct ScopedWsSession : RWsSession {
    ~ScopedWsSession() { Close(); }
};

class Controls {
    ScopedWsSession ws_;
    RProcess game_;
    TRequestStatus game_exit_;
    bool watching_game_ = false;
    std::unique_ptr<CWsScreenDevice> screen_;
    RWindowGroup group_;
    RTimer timer_;
    // A single pixel that captures every touch; the posting surface draws the controls.
    std::unique_ptr<RWindow> capture_;
    RProperty state_;
    bool state_defined_ = false;
    TInt published_ = -1;
    Ngage::TouchLayout layout_{TSize(0, 0)};
    // Buttons each pointer holds; a pad touch that started on OK stays there until it leaves.
    std::array<unsigned, 16> pointers_{};
    std::array<bool, 16> on_ok_{};
    std::array<int, 16> origin_{};
    std::array<int, kButtonCount> held_{};
    std::array<unsigned, kButtonCount> repeat_at_{};
    TRequestStatus events_, tick_;
    TSize size_{0, 0};
    TInt target_ = 0;
    TInt last_focus_ = -1;
    unsigned clock_ = 0;
    bool active_ = false;
    bool hidden_ = false;
    bool keypad_ = false;
    bool requests_ = false;
    enum : int { kNowhere = -1, kPad = -2 };

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

    void Publish() {
        TInt value = (active_ ? Ngage::kTouchActive : 0) | (hidden_ ? Ngage::kTouchHidden : 0) |
                     (keypad_ ? Ngage::kTouchKeypadShown : 0);
        for (int i = 0; i < kButtonCount; ++i) if (held_[i]) value |= Bit(i);
        if (value == published_ || !state_.Handle()) return;
        published_ = value;
        state_.Set(value);
    }

    void ReleaseAll() {
        for (int i = 0; i < kButtonCount; ++i) {
            if (held_[i]) Send(i, EEventKeyUp);
            held_[i] = 0;
        }
        pointers_.fill(0);
        Publish();
    }

    void Visible() {
        capture_->SetVisible(active_);
        capture_->SetPointerCapture(active_ ? RWindowBase::TCaptureFlagEnabled | RWindowBase::TCaptureFlagAllGroups
                                            : RWindowBase::TCaptureDisabled);
        Publish();
    }

    void Layout() {
        screen_->SetAppScreenMode(screen_->CurrentScreenMode());
        const auto size = screen_->SizeInPixels();
        if (size == size_) return;
        ReleaseAll();
        size_ = size;
        layout_ = Ngage::TouchLayout(size);
    }

    void Press(int pointer, unsigned buttons) {
        const unsigned old = pointers_[pointer];
        if (old == buttons) return;
        pointers_[pointer] = buttons;
        for (int i = 0; i < kButtonCount; ++i) {
            if ((old & ~buttons & Bit(i)) && --held_[i] == 0) Send(i, EEventKeyUp);
        }
        for (int i = 0; i < kButtonCount; ++i) {
            if ((buttons & ~old & Bit(i)) && held_[i]++ == 0) {
                Send(i, EEventKeyDown);
                Send(i, EEventKey);
                repeat_at_[i] = clock_ + 4;
            }
        }
        Publish();
    }

    // Eight directions by angle from the pad centre; a diagonal holds both of its keys.
    unsigned PadButtons(int pointer, const TPoint& position, bool down) {
        const int unit = layout_.unit;
        const int dx = position.iX - layout_.pad_center.iX, dy = position.iY - layout_.pad_center.iY;
        const int ax = std::abs(dx), ay = std::abs(dy), reach = std::max(ax, ay);
        if (down) on_ok_[pointer] = reach < unit / 2;
        // OK needs a touch that starts on it; sliding through the centre keeps the direction.
        if (on_ok_[pointer]) {
            if (reach < unit * 3 / 4) return Bit(Ngage::kTouchOk);
            on_ok_[pointer] = false;
        }
        if (reach < unit / 4) return pointers_[pointer] & ~Bit(Ngage::kTouchOk);
        unsigned buttons = 0;
        // tan(22.5 degrees) is about 5/12.
        if (ay * 12 > ax * 5) buttons |= Bit(dy < 0 ? Ngage::kTouchUp : Ngage::kTouchDown);
        if (ax * 12 > ay * 5) buttons |= Bit(dx < 0 ? Ngage::kTouchLeft : Ngage::kTouchRight);
        return buttons;
    }

    void Pointer(const TWsEvent& event) {
        if (!active_) return;
        const auto* pointer = event.Pointer();
        const int number = pointer->PointerNumber();
        if (number < 0 || number >= static_cast<int>(pointers_.size())) return;
        if (pointer->iType == TPointerEvent::EButton1Up) {
            Press(number, 0);
            origin_[number] = kNowhere;
            return;
        }
        const bool down = pointer->iType == TPointerEvent::EButton1Down;
        if (!down && pointer->iType != TPointerEvent::EDrag) return;
        // The capture window is at the screen origin, so its coordinates are the screen's.
        const TPoint position = pointer->iPosition;
        if (down) {
            origin_[number] = kNowhere;
            if (layout_.toggle.Contains(position)) {
                ReleaseAll();
                hidden_ = !hidden_;
                Publish();
                return;
            }
            if (hidden_) return;
            if (layout_.mode.Contains(position)) {
                ReleaseAll();
                keypad_ = !keypad_;
                Publish();
                return;
            }
            const TInt shown = keypad_ ? Ngage::kTouchKeypadShown : 0;
            if (layout_.PadArea().Contains(position)) origin_[number] = kPad;
            for (int i = Ngage::kTouchPadButtons; i < kButtonCount; ++i)
                if (Ngage::TouchButtonShown(i, shown) && layout_.buttons[i].Contains(position)) origin_[number] = i;
        }
        const int origin = origin_[number];
        if (origin == kPad) {
            Press(number, PadButtons(number, position, down));
        } else if (origin >= 0) {
            Press(number, layout_.buttons[origin].Contains(position) ? Bit(origin) : 0);
        }
    }

    void Poll() {
        ++clock_;
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
    Controls() = default;
    int Run(TProcessId game_id) {
        TInt result = game_.Open(game_id);
        if (result != KErrNone) return result;
        if (game_.ExitType() != EExitPending) return KErrNone;
        game_.Logon(game_exit_);
        watching_game_ = true;
        if (game_exit_ != KRequestPending)
            return game_.ExitType() == EExitPending ? game_exit_.Int() : KErrNone;
        result = ws_.Connect();
        if (result != KErrNone) return result;
        screen_ = std::make_unique<CWsScreenDevice>(ws_);
        if ((result = screen_->Construct()) != KErrNone) return result;
        group_ = RWindowGroup(ws_);
        if ((result = group_.Construct(1, EFalse)) != KErrNone) return result;
        group_.EnableReceiptOfFocus(EFalse);
        group_.SetOrdinalPosition(0, 500);
        capture_ = std::make_unique<RWindow>(ws_);
        if ((result = capture_->Construct(group_, 2)) != KErrNone) return result;
        capture_->SetExtent(TPoint(0, 0), TSize(1, 1));
        capture_->EnableAdvancedPointers();
        // Windows do not receive drag events unless the filter is cleared.
        capture_->PointerFilter(EPointerFilterDrag, 0);
        capture_->SetVisible(EFalse);
        capture_->Activate();
        const TUid category = {static_cast<TInt32>(Ngage::kTouchServiceSid)};
        const TUint key = static_cast<TUint>(game_id.Id());
        static _LIT_SECURITY_POLICY_PASS(read_policy);
        static _LIT_SECURITY_POLICY_S0(write_policy, Ngage::kTouchServiceSid);
        result = RProperty::Define(category, key, RProperty::EInt, read_policy, write_policy);
        if (result != KErrNone && result != KErrAlreadyExists) return result;
        state_defined_ = true;
        if ((result = state_.Attach(category, key)) != KErrNone) return result;
        origin_.fill(kNowhere);
        if ((result = timer_.CreateLocal()) != KErrNone) return result;
        Layout();
        Publish();
        ws_.EventReady(&events_);
        timer_.After(tick_, 100000);
        requests_ = true;
        ws_.Flush();
        for (;;) {
            User::WaitForAnyRequest();
            if (game_exit_ != KRequestPending) return KErrNone;
            if (events_ != KRequestPending) {
                if (events_.Int() != KErrNone) return events_.Int();
                TWsEvent event;
                ws_.GetEvent(event);
                if (event.Type() == EEventPointer) Pointer(event);
                ws_.EventReady(&events_);
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
        if (requests_) {
            timer_.Cancel();
            ws_.EventReadyCancel();
            if (tick_ == KRequestPending) User::WaitForRequest(tick_);
            if (events_ == KRequestPending) User::WaitForRequest(events_);
        }
        timer_.Close();
        if (state_defined_) {
            state_.Close();
            const TUid category = {static_cast<TInt32>(Ngage::kTouchServiceSid)};
            RProperty::Delete(category, static_cast<TUint>(game_.Id().Id()));
        }
        game_.Close();
        if (capture_) capture_->Close();
        group_.Close();
    }
};
}

int main() {
    TBuf<32> command;
    if (User::CommandLineLength() > command.MaxLength()) return KErrArgument;
    User::CommandLine(command);
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
    single.Close();
    return result;
}
