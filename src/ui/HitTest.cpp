#include "ui/HitTest.h"

#include "ui/Layout.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

int CaptionButtonAt(POINT p) {
    for (int i = 0; i < 3; ++i)
        if (In(g_L.caption[i], p)) return i;
    return -1;
}

int HitRow(POINT p) {
    if (!In(g_L.list, p)) return -1;
    int row = (p.y - g_L.list.top + g_scroll) / g_L.rowH, col = 0;
    if (g_L.grid) {
        int x = p.x - (g_L.list.left + S(kMargin) - g_L.cardPad);
        if (x < 0) return -1;
        col = x / (g_L.cellW + g_L.cardGap);
        if (col >= g_L.cols) return -1;
    }
    int index = row * g_L.cols + col;
    if (row < 0 || index >= (int)g_visible.size()) return -1;
    return In(ItemCell(index), p) ? index : -1;  // the gaps between cards hit nothing
}

int HitButton(POINT p) {
    for (int i = 0; i < BTN_COUNT; ++i)
        if (In(g_L.btn[i], p)) return i;
    return -1;
}

int HitChip(POINT p) {
    if (!In(g_L.sideList, p)) return -1;  // rows scrolled out of the sidebar are not clickable
    for (int i = 0; i < (int)g_L.chips.size(); ++i)
        if (In(g_L.chips[i], p)) return i;
    return -1;
}

void UpdateHover(HWND hwnd, POINT p) {
    Hover next;
    next.row = HitRow(p);
    next.button = HitButton(p);
    next.chip = HitChip(p);
    next.cover = InPlayZone(next.row, p);
    next.sideCreate = In(g_L.create, p);
    next.sortBtn = In(g_L.sortBtn, p);
    next.searchClear = !g_search.empty() && In(g_L.searchClear, p);
    next.toggle = In(g_L.toggle, p);
    if (g_menu.open) {
        // The menu sits on top, so it hides whatever is underneath.
        next = Hover{};
        for (int i = 0; i < g_menu.count; ++i)
            if (In(g_menu.items[i], p)) next.menuItem = i;
    }
    bool changed = next.row != g_hover.row || next.button != g_hover.button || next.chip != g_hover.chip ||
        next.cover != g_hover.cover || next.menuItem != g_hover.menuItem || next.sideCreate != g_hover.sideCreate ||
        next.sortBtn != g_hover.sortBtn || next.searchClear != g_hover.searchClear ||
        next.toggle != g_hover.toggle;
    if (!changed) return;

    // Repaint only what changed: the cards that lose and gain the highlight, the buttons, the chips.
    Hover previous = g_hover;
    g_hover = next;
    if (g_menu.open || previous.menuItem != next.menuItem) { Redraw(hwnd); return; }
    auto touch = [&](RECT r, int margin) {
        InflateRect(&r, margin, margin);
        InvalidateRect(hwnd, &r, FALSE);
    };
    for (int row : { previous.row, next.row })
        if (row >= 0 && row < (int)g_visible.size()) touch(ItemCell(row), S(6));
    for (int button : { previous.button, next.button })
        if (button >= 0) touch(g_L.btn[button], S(2));
    for (int chip : { previous.chip, next.chip })
        if (chip >= 0 && chip < (int)g_L.chips.size()) touch(g_L.chips[chip], S(2));
    if (previous.sideCreate != next.sideCreate) touch(g_L.create, S(2));
    if (previous.sortBtn != next.sortBtn) touch(g_L.sortBtn, S(2));
    if (previous.searchClear != next.searchClear) touch(g_L.searchClear, S(2));
    if (previous.toggle != next.toggle) touch(g_L.toggle, S(2));
}

}  // namespace playr
