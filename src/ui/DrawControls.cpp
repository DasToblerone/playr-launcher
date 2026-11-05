#include "ui/Draw.h"

#include <cmath>

#include "model/Library.h"
#include "ui/DrawPrimitives.h"
#include "ui/Format.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

namespace {

// Spinner, animated check mark or text, centered in a button.
void DrawBadge(Graphics& g, RectF r, const Badge& badge, const Color& ink = kText, const Color& accent = kAccentHover) {
    RectF box;
    g.MeasureString(badge.text.c_str(), -1, &CachedFont((REAL)S(13), true), PointF(0, 0), &box);
    bool hasIcon = badge.mode == Badge::Spinner || badge.mode == Badge::Check;
    REAL icon = hasIcon ? (REAL)S(16) : 0.0f, gap = hasIcon ? (REAL)S(6) : 0.0f, textW = box.Width - S(8);
    REAL startX = r.X + (r.Width - (icon + gap + textW)) / 2;
    REAL cx = startX + icon / 2, cy = r.Y + r.Height / 2;

    Pen pen(accent, (REAL)S(2));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    if (badge.mode == Badge::Spinner) {
        REAL angle = (REAL)(GetTickCount64() % 800) / 800.0f * 360.0f;
        g.DrawArc(&pen, cx - icon / 2 + 1, cy - icon / 2 + 1, icon - 2, icon - 2, angle, 270);
    } else if (badge.mode == Badge::Check) {
        double t = std::min(1.0, (double)(GetTickCount64() - badge.since) / 260.0);
        REAL drawn = (REAL)(1.0 - std::pow(1.0 - t, 3.0));
        REAL k = (REAL)S(1);
        PointF a(cx - 5 * k, cy + 0.5f * k), b(cx - 1.5f * k, cy + 4 * k), c(cx + 6 * k, cy - 4 * k);
        REAL l1 = std::hypot(b.X - a.X, b.Y - a.Y), l2 = std::hypot(c.X - b.X, c.Y - b.Y);
        REAL length = drawn * (l1 + l2);
        REAL f1 = std::min(length, l1) / l1;
        g.DrawLine(&pen, a, PointF(a.X + (b.X - a.X) * f1, a.Y + (b.Y - a.Y) * f1));
        if (length > l1) {
            REAL f2 = (length - l1) / l2;
            g.DrawLine(&pen, b, PointF(b.X + (c.X - b.X) * f2, b.Y + (c.Y - b.Y) * f2));
        }
    }
    DrawLabel(g, badge.text, (REAL)S(13), true, ink,
        RectF(startX + icon + gap, r.Y, textW + S(12), r.Height), StringAlignmentNear, StringAlignmentCenter);
}

}  // namespace

// Switch that shows or hides the games that are not installed.
void DrawUninstalledToggle(Graphics& g) {
    RectF r = RF(g_L.toggle);
    bool on = g_showUninstalled, hover = g_hover.toggle;
    REAL trackW = (REAL)S(38), trackH = (REAL)S(22);
    RectF track(r.GetRight() - trackW, r.Y + (r.Height - trackH) / 2, trackW, trackH);
    FillRound(g, track, trackH / 2, on ? (hover ? kAccentHover : kAccent) : (hover ? Color(255, 110, 110, 110) : Color(255, 84, 84, 84)));
    REAL knob = trackH - S(6);
    SolidBrush brush(Color(255, 255, 255, 255));
    g.FillEllipse(&brush, on ? track.GetRight() - knob - S(3) : track.X + S(3), track.Y + (trackH - knob) / 2, knob, knob);
    if (r.Width > S(80))
        DrawLabel(g, L"Nicht installierte", (REAL)S(13), true, hover ? kText : kText2,
            RectF(r.X, r.Y, r.Width - trackW - S(10), r.Height), StringAlignmentFar, StringAlignmentCenter);
}

void DrawSortButton(Graphics& g) {
    RectF r = RF(g_L.sortBtn);
    FillRound(g, r, r.Height / 2, g_hover.sortBtn ? kChipHover : kSurface);
    DrawButtonContent(g, r, Icon::Sort, std::wstring(SortName(g_sortKey)) + L"  " + SortArrow(g_sortDescending), kText);
}

// "Neu scannen" and "Steam-Sammlungen": in the bottom bar of the library, in the top bar of the randomizer.
void DrawActionButtons(Graphics& g) {
    for (int i = 0; i < BTN_COUNT; ++i) {
        RectF r = RF(g_L.btn[i]);
        bool busy = g_badge[i].mode != Badge::Off;
        FillRound(g, r, r.Height / 2, g_hover.button == i && !busy ? kChipHover : kSurface);
        if (busy) DrawBadge(g, r, g_badge[i]);
        else DrawButtonContent(g, r, kButtonIcons[i], kButtonLabels[i], kText);
    }
}

void DrawBar(Graphics& g, int W, int H) {
    SolidBrush bar(kBar);
    g.FillRectangle(&bar, 0, (INT)g_L.bar.top, W, H - (INT)g_L.bar.top);
    DrawActionButtons(g);
}

}  // namespace playr
