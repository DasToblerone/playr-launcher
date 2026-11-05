#include "ui/HitTest.h"

#include "ui/Layout.h"
#include "ui/Randomizer.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

int CaptionButtonAt(POINT p) {
    for (int i = 0; i < 3; ++i)
        if (In(g_L.caption[i], p)) return i;
    return -1;
}

int HitTab(POINT p) {
    for (int i = 0; i < 2; ++i)
        if (In(g_L.tab[i], p)) return i;
    return -1;
}

int HitButton(POINT p) {
    for (int i = 0; i < BTN_COUNT; ++i)
        if (In(g_L.btn[i], p)) return i;  // buttons the page does not show have an empty rectangle
    return -1;
}

int HitRow(POINT p) {
    if (g_page != Page::Library || !In(g_L.list, p)) return -1;
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

int HitChip(POINT p) {
    if (g_page != Page::Library || !In(g_L.sideList, p)) return -1;  // rows scrolled out of the sidebar are not clickable
    for (int i = 0; i < (int)g_L.chips.size(); ++i)
        if (In(g_L.chips[i], p)) return i;
    return -1;
}

int HitRollChip(POINT p) {
    if (g_page != Page::Randomizer) return -1;
    for (int i = 0; i < (int)g_L.rollChips.size(); ++i)
        if (In(g_L.rollChips[i].r, p)) return i;
    return -1;
}

int HitAction(POINT p) {
    if (g_page != Page::Randomizer) return -1;
    for (int i = 0; i < ACT_COUNT; ++i)
        if (ActionActive(i) && In(g_L.act[i], p)) return i;
    return -1;
}

bool HitCard(POINT p) { return g_page == Page::Randomizer && !Spinning() && ResultGame() && In(CenterCard(), p); }

void UpdateHover(HWND hwnd, POINT p) {
    Hover next;
    next.tab = HitTab(p);
    next.button = HitButton(p);
    if (g_page == Page::Randomizer) {
        next.rollChip = HitRollChip(p);
        next.action = HitAction(p);
        next.rollToggle = In(g_L.rollToggle, p);
        next.card = HitCard(p);
        if (next.tab == g_hover.tab && next.button == g_hover.button && next.rollChip == g_hover.rollChip &&
            next.action == g_hover.action && next.rollToggle == g_hover.rollToggle && next.card == g_hover.card)
            return;
        // Repaint only the chips, buttons and tabs that lose or gain the highlight, not the page with its covers.
        Hover previous = g_hover;
        g_hover = next;
        auto touch = [&](RECT r) {
            InflateRect(&r, S(2), S(2));
            InvalidateRect(hwnd, &r, FALSE);
        };
        for (int tab : { previous.tab, next.tab })
            if (tab >= 0) touch(g_L.tab[tab]);
        for (int button : { previous.button, next.button })
            if (button >= 0) touch(g_L.btn[button]);
        for (int chip : { previous.rollChip, next.rollChip })
            if (chip >= 0 && chip < (int)g_L.rollChips.size()) touch(g_L.rollChips[chip].r);
        for (int action : { previous.action, next.action })
            if (action >= 0) touch(g_L.act[action]);
        if (previous.rollToggle != next.rollToggle) touch(g_L.rollToggle);
        return;
    }

    next.row = HitRow(p);
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
        next.toggle != g_hover.toggle || next.tab != g_hover.tab;
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
    for (int tab : { previous.tab, next.tab })
        if (tab >= 0) touch(g_L.tab[tab], S(2));
    if (previous.sideCreate != next.sideCreate) touch(g_L.create, S(2));
    if (previous.sortBtn != next.sortBtn) touch(g_L.sortBtn, S(2));
    if (previous.searchClear != next.searchClear) touch(g_L.searchClear, S(2));
    if (previous.toggle != next.toggle) touch(g_L.toggle, S(2));
}

}  // namespace playr
