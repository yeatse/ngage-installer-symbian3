#pragma once
namespace Ngage {
inline unsigned DivideVolume(unsigned value) {
    unsigned quotient = 0, remainder = 0;
    for (int bit = 31; bit >= 0; --bit) {
        remainder = (remainder << 1) | ((value >> bit) & 1);
        if (remainder >= 10) { remainder -= 10; quotient |= 1u << bit; }
    }
    return quotient;
}
inline int ScaleLegacyVolume(int requested, int maximum) {
    if (maximum <= 0 || requested <= 0) return 0;
    if (requested >= 10) return maximum;
    const unsigned quotient = DivideVolume(static_cast<unsigned>(maximum));
    const unsigned remainder = static_cast<unsigned>(maximum) - quotient * 10;
    return quotient * requested + DivideVolume(remainder * requested + 5);
}
}
