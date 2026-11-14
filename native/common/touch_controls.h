#pragma once
#include <e32std.h>
#include <gdi.h>

// Shared by the touch service, which reads the touches, and the posting surface. Over a compositor
// surface the service draws the controls in a transparent window; a game drawing through the DSA
// buffer cannot draw under a window, so there the posting surface blends them into each frame.
namespace Ngage {
constexpr TUint32 kTouchServiceSid = 0xe0000960;
enum TouchButton { kTouchUp, kTouchLeft, kTouchOk, kTouchRight, kTouchDown, kTouchPadButtons,
                   kTouchA = kTouchPadButtons, kTouchB, kTouchL, kTouchR,
                   // Phone keypad in rows: 1 2 3, 4 5 6, 7 8 9, * 0 #, then its clear key.
                   kTouchKeypad, kTouchClear = kTouchKeypad + 12, kTouchButtons };
// Property value: one bit per held button, then these flags. The key is the game's process ID.
constexpr TInt kTouchHidden = 1 << kTouchButtons, kTouchActive = 2 << kTouchButtons,
               kTouchKeypadShown = 4 << kTouchButtons;
// The posting surface sets this property of the game's SID to 1 when it shows a compositor surface.
constexpr TUint kTouchCompositorKey = kTouchServiceSid;
// The keypad takes the place of A and B; the pad, L and R stay.
inline TBool TouchButtonShown(TInt button, TInt state) {
    if (button == kTouchA || button == kTouchB) return !(state & kTouchKeypadShown);
    return button < kTouchKeypad || (state & kTouchKeypadShown);
}

inline TPtrC TouchLabel(TInt button) {
    static const char16_t* const labels[kTouchButtons] = {
        u"↑", u"←", u"OK", u"→", u"↓", u"A", u"B", u"L", u"R",
        u"1", u"2", u"3", u"4", u"5", u"6", u"7", u"8", u"9", u"*", u"0", u"#", u"C"};
    return TPtrC(reinterpret_cast<const TUint16*>(labels[button]));
}

struct TouchLayout {
    TInt unit;
    TPoint pad_center;
    TRect toggle;
    TRect mode;
    TRect buttons[kTouchButtons];

    explicit TouchLayout(const TSize& screen) {
        // The posting surface links no division routine: n * 9363 >> 16 is n / 7 for screen sizes.
        unit = (Min(screen.iWidth, screen.iHeight) * 9363) >> 16;
        unit = Max(28, Min(48, unit));
        const TInt x = 6, y = (screen.iHeight - 3 * unit) / 2;
        pad_center = TPoint(x + unit * 3 / 2, y + unit * 3 / 2);
        static const TInt8 column[kTouchPadButtons] = {1, 0, 1, 2, 1}, row[kTouchPadButtons] = {0, 1, 1, 1, 2};
        for (TInt i = 0; i < kTouchPadButtons; ++i)
            buttons[i] = TRect(TPoint(x + column[i] * unit, y + row[i] * unit), TSize(unit, unit));
        const TInt right = screen.iWidth - unit - 8;
        buttons[kTouchA] = TRect(right, y + unit / 2, right + unit, y + unit * 3 / 2);
        buttons[kTouchB] = TRect(right, y + unit * 3 / 2 + 8, right + unit, y + unit * 5 / 2 + 8);
        buttons[kTouchL] = TRect(right, screen.iHeight - unit - 8, right + unit, screen.iHeight - 8);
        buttons[kTouchR] = TRect(right, 8, right + unit, 8 + unit);
        toggle = TRect(6, 8, 38, 40);
        mode = TRect(6, screen.iHeight - 40, 38, screen.iHeight - 8);
        const TInt left = screen.iWidth - 8 - 3 * unit, top = (screen.iHeight - 5 * unit) / 2;
        for (TInt i = 0; i < 12; ++i) {
            const TInt keypad_row = (i * 11) >> 5;  // i / 3 without a division routine
            buttons[kTouchKeypad + i] = TRect(TPoint(left + (i - keypad_row * 3) * unit, top + keypad_row * unit),
                                              TSize(unit, unit));
        }
        buttons[kTouchClear] = TRect(TPoint(left, top + 4 * unit), TSize(unit, unit));
    }
    // The pad takes touches in its corners and a little beyond the drawn cross.
    TRect PadArea() const {
        const TInt reach = unit * 2 - unit / 8;
        return TRect(pad_center - TPoint(reach, reach), pad_center + TPoint(reach, reach));
    }
};

// Drawn items: the buttons, then the switch that hides them and the one that swaps in the keypad.
enum { kTouchToggle = kTouchButtons, kTouchMode, kTouchItems };
inline TRect TouchItemRect(const TouchLayout& layout, TInt item) {
    return item == kTouchToggle ? layout.toggle : item == kTouchMode ? layout.mode : layout.buttons[item];
}
inline TBool TouchItemShown(TInt item, TInt state) {
    if (!(state & kTouchActive)) return EFalse;
    if (item == kTouchToggle) return ETrue;
    if (state & kTouchHidden) return EFalse;
    return item == kTouchMode || TouchButtonShown(item, state);
}
inline void DrawTouchItem(CGraphicsContext& gc, const CFont& font, const TRect& bounds, TInt item, TInt state) {
    TRect area = bounds;
    area.Shrink(1, 1);
    const TBool pressed = item < kTouchButtons && (state & (1 << item));
    gc.SetPenStyle(CGraphicsContext::ENullPen);
    gc.SetBrushColor(pressed ? TRgb(230, 130, 20, 180) : TRgb(24, 32, 40, 140));
    gc.DrawRect(area);
    gc.SetPenStyle(CGraphicsContext::ESolidPen);
    gc.SetPenColor(TRgb(255, 255, 255, 240));
    if (item == kTouchToggle) {
        const TPoint center = bounds.Center();
        gc.SetPenSize(TSize(2, 2));
        gc.DrawLine(center - TPoint(6, 0), center + TPoint(7, 0));
        if (state & kTouchHidden) gc.DrawLine(center - TPoint(0, 6), center + TPoint(0, 7));
        gc.SetPenSize(TSize(1, 1));
        return;
    }
    // The switch names the keys it brings up.
    const TPtrC text = item != kTouchMode ? TouchLabel(item) :
        (state & kTouchKeypadShown) ? TPtrC(_L("A")) : TPtrC(_L("1"));
    const TInt baseline = (bounds.Height() - font.HeightInPixels()) / 2 + font.AscentInPixels();
    gc.DrawText(text, bounds, baseline, CGraphicsContext::ECenter, 0);
}
}
