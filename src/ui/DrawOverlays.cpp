#include "ui/Draw.h"

#include <algorithm>
#include <cmath>

#include "model/Library.h"
#include "ui/DrawPrimitives.h"
#include "ui/Format.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

void DrawDragGhost(Graphics& g) {
    if (!g_drag.active || g_drag.row < 0 || g_drag.row >= (int)g_visible.size()) return;
    RectF ghost((REAL)g_drag.pos.x + S(14), (REAL)g_drag.pos.y + S(10), (REAL)S(220), (REAL)S(40));
    FillRound(g, RectF(ghost.X + 1, ghost.Y + 3, ghost.Width, ghost.Height), (REAL)S(20), Color(70, 0, 0, 0));
    FillRound(g, ghost, (REAL)S(20), kAccent);
    DrawLabel(g, g_games[g_visible[g_drag.row]].name, (REAL)S(14), true, kOnAccent,
        RectF(ghost.X + S(18), ghost.Y, ghost.Width - S(36), ghost.Height), StringAlignmentNear, StringAlignmentCenter);
}

void DrawMenu(Graphics& g) {
    if (!g_menu.open) return;
    RectF r = RF(g_menu.rect);
    FillRound(g, RectF(r.X, r.Y + 4, r.Width, r.Height), (REAL)S(10), Color(90, 0, 0, 0));
    FillRound(g, r, (REAL)S(10), kChipHover);
    FillRound(g, RectF(r.X + 1, r.Y + 1, r.Width - 2, r.Height - 2), (REAL)S(9), Color(255, 36, 36, 36));
    for (int i = 0; i < g_menu.count; ++i) {
        RectF item = RF(g_menu.items[i]);
        bool hover = i == g_hover.menuItem;
        if (hover) FillRound(g, item, (REAL)S(6), kChipHover);
        Menu::Action action = g_menu.actions[i];
        std::wstring label;
        Color text = kText;
        switch (action) {
        case Menu::Rename: label = L"Umbenennen"; break;
        case Menu::Delete: label = L"Löschen"; text = hover ? kDangerHover : kDanger; break;
        case Menu::RemoveFromCategory:
            label = L"Aus „" + g_categories[g_menu.chip] + L"“ entfernen";
            text = hover ? kDangerHover : kDanger;
            break;
        default: {  // sort options: the current one is marked with the accent color and its direction
            SortKey key = SortKeyOf(action);
            label = SortName(key);
            if (key == g_sortKey) {
                label += std::wstring(L"  ") + SortArrow(g_sortDescending);
                text = kAccentHover;
            }
        }
        }
        DrawLabel(g, label, (REAL)S(14), false, text,
            RectF(item.X + S(12), item.Y, item.Width - S(24), item.Height), StringAlignmentNear, StringAlignmentCenter);
    }
}

// Fades and slides in, holds, then fades out.
void DrawToast(Graphics& g) {
    if (!g_toast.active) return;
    constexpr double kFade = 200.0;
    double elapsed = (double)(GetTickCount64() - g_toast.since);
    double in = std::min(1.0, elapsed / kFade);
    double out = std::min(1.0, ((double)g_toast.duration - elapsed) / kFade);
    REAL alpha = (REAL)std::clamp(std::min(in, out), 0.0, 1.0);
    REAL slide = (REAL)((1.0 - (1.0 - std::pow(1.0 - in, 3.0))) * S(12));

    RectF r = RF(g_toast.rect);
    r.Y += slide;
    FillRound(g, RectF(r.X, r.Y + 3, r.Width, r.Height), r.Height / 2, Color((BYTE)(70 * alpha), 0, 0, 0));
    FillRound(g, r, r.Height / 2, WithAlpha(kChipHover, alpha));

    DrawLabel(g, g_toast.text, (REAL)S(14), false, WithAlpha(kText, alpha),
        RectF(r.X + S(22), r.Y, r.Width - S(44), r.Height), StringAlignmentNear, StringAlignmentCenter);
}

}  // namespace playr
