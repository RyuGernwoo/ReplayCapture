#pragma once
#include <windows.h>

namespace replay::ui {
inline bool highContrast() {
    HIGHCONTRASTW value{sizeof(value)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(value), &value, 0);
    return (value.dwFlags & HCF_HIGHCONTRASTON) != 0;
}
inline COLORREF background() {
    return highContrast() ? GetSysColor(COLOR_WINDOW) : RGB(255, 255, 255);
}
inline COLORREF foreground() {
    return highContrast() ? GetSysColor(COLOR_WINDOWTEXT) : RGB(23, 32, 51);
}
inline COLORREF accent() {
    return highContrast() ? GetSysColor(COLOR_HIGHLIGHT) : RGB(36, 87, 214);
}
inline COLORREF accentText() {
    return highContrast() ? GetSysColor(COLOR_HIGHLIGHTTEXT) : RGB(255, 255, 255);
}
} // namespace replay::ui
