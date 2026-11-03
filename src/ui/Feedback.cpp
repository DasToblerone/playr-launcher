#include "ui/Feedback.h"

#include <algorithm>
#include <cmath>

#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

void LayoutToast(HWND hwnd) {
    Graphics g(hwnd);
    FontFamily family(L"Arial");
    Font regular(&family, (REAL)S(14), FontStyleRegular, UnitPixel);
    RectF textBox;
    g.MeasureString(g_toast.text.c_str(), -1, &regular, PointF(0, 0), &textBox);

    RECT client;
    GetClientRect(hwnd, &client);
    int pad = S(22), h = S(46);
    int w = std::min(pad * 2 + (int)std::ceil(textBox.Width) + S(2), (int)client.right - S(32));
    int x = (client.right - w) / 2, y = g_L.bar.top - S(14) - h;
    g_toast.rect = { x, y, x + w, y + h };
}

void ShowToast(HWND hwnd, const std::wstring& text) {
    g_toast.active = true;
    g_toast.text = text;
    g_toast.since = GetTickCount64();
    g_toast.duration = 3500;
    LayoutToast(hwnd);
    SetTimer(hwnd, TIMER_ANIMATION, 16, nullptr);
    Redraw(hwnd);
}

// Keeps the animation timer running while any button shows a badge.
void ShowBadge(HWND hwnd, int button, Badge::Mode mode, const std::wstring& text, ULONGLONG hideAfter) {
    g_badge[button] = { mode, text, GetTickCount64(), hideAfter };
    SetTimer(hwnd, TIMER_ANIMATION, 16, nullptr);
    InvalidateRect(hwnd, &g_L.btn[button], FALSE);
}

void TickBadges(HWND hwnd) {
    ULONGLONG now = GetTickCount64();
    bool anyActive = false;
    for (int i = 0; i < BTN_COUNT; ++i) {
        Badge& badge = g_badge[i];
        if (badge.mode == Badge::Off) continue;
        if (badge.mode == Badge::Spinner) {
            // Keep spinners visible long enough to notice, even when the work is already done.
            if (i == BTN_SCAN && g_scanReady && now - badge.since >= 700) {
                badge = { Badge::Check, L"Fertig", now, 1800 };
            } else if (i == BTN_IMPORT && g_importReady && now - badge.since >= 700) {
                badge = g_importResult;
                badge.since = now;
                g_importReady = false;
            }
        } else if (now - badge.since >= badge.hideAfter) {
            badge.mode = Badge::Off;
        }
        anyActive |= badge.mode != Badge::Off;
        InvalidateRect(hwnd, &g_L.btn[i], FALSE);
    }
    if (g_toast.active) {
        RECT area = g_toast.rect;
        InflateRect(&area, S(24), S(24));
        InvalidateRect(hwnd, &area, FALSE);
        if (now - g_toast.since >= g_toast.duration) g_toast.active = false;
        anyActive |= g_toast.active;
    }
    if (!anyActive) KillTimer(hwnd, TIMER_ANIMATION);
}

}  // namespace playr
