#pragma once
#include <e32std.h>

// Shared by the touch service, which reads the touches, and the posting surface, which draws the
// controls into each game frame: the game cannot draw under a window, so none covers it.
namespace Ngage {
constexpr TUint32 kTouchServiceSid = 0xe0000960;
enum TouchButton { kTouchUp, kTouchLeft, kTouchOk, kTouchRight, kTouchDown, kTouchPadButtons,
                   kTouchA = kTouchPadButtons, kTouchB, kTouchL, kTouchR,
                   // Phone keypad in rows: 1 2 3, 4 5 6, 7 8 9, * 0 #, then its clear key.
                   kTouchKeypad, kTouchClear = kTouchKeypad + 12, kTouchButtons };
// Property value: one bit per held button, then these flags. The key is the game's process ID.
constexpr TInt kTouchHidden = 1 << kTouchButtons, kTouchActive = 2 << kTouchButtons,
               kTouchKeypadShown = 4 << kTouchButtons;
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
}
