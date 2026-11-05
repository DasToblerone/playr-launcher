#include "ui/Layout.h"

#include <algorithm>
#include <cmath>

#include "model/Library.h"
#include "ui/DrawPrimitives.h"
#include "ui/Feedback.h"
#include "ui/LibraryLayout.h"
#include "ui/Randomizer.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

namespace {

constexpr float kCenterScale = 1.08f;  // the card in the middle is drawn this much larger

int TextWidth(Graphics& g, const std::wstring& text) {
    RectF box;
    g.MeasureString(text.c_str(), -1, &CachedFont((REAL)S(13), true), PointF(0, 0), StringFormat::GenericTypographic(), &box);
    return (int)std::ceil(box.Width);
}

// Lays the chips of one group out in lines that wrap at `maxRight`; returns the bottom of the last line.
int FlowChips(Graphics& g, int group, int startX, int top, int maxRight, int maxLines) {
    int height = S(kChipHeight), x = startX, y = top, lines = 1;
    int count = group == 0 ? kPlayFilterCount : (int)g_categories.size();
    for (int i = 0; i < count; ++i) {
        std::wstring label = group == 0 ? PlayFilterName((PlayFilter)i) : g_categories[i];
        int width = std::min(TextWidth(g, label) + S(32), S(kChipMaxWidth));
        if (x + width > maxRight && x > startX) {
            if (++lines > maxLines) break;
            x = startX;
            y += height + S(kChipGap);
        }
        ChipHit chip;
        chip.r = { x, y, x + width, y + height };
        chip.group = group;
        chip.value = i;
        chip.label = label;
        g_L.rollChips.push_back(chip);
        x += width + S(kChipGap);
    }
    return y + height;
}

// The randomizer page: the filters on top, the reel with the result under them.
void LayoutRandomizer(HWND hwnd, int W, int H) {
    int gap = S(kPanelGap), top = S(kTopBar);
    g_L.panel = { gap, top, W - gap, std::max(top + S(200), H - gap) };
    g_L.toastBottom = g_L.panel.bottom - S(24);
    int pad = S(kPanelPad), left = g_L.panel.left + pad, right = g_L.panel.right - pad;

    // Filters: the play-time chips with the switch at the right, then the collections.
    g_L.rollChips.clear();
    Graphics g(hwnd);
    int height = S(kChipHeight), labelW = S(kLabelWidth);
    int y = g_L.panel.top + S(22);
    int toggleW = S(190);
    g_L.rollToggle = { right - toggleW, y, right, y + height };
    g_L.labelX = left;
    g_L.labelH = height;
    g_L.labelY[0] = y;
    int filtersBottom = FlowChips(g, 0, left + labelW, y, right - toggleW - S(16), 3);
    g_L.labelY[1] = -1;
    if (g_categories.size() > 1) {
        g_L.labelY[1] = filtersBottom + S(10);
        filtersBottom = FlowChips(g, 1, left + labelW, g_L.labelY[1], right, 2);
    }

    // The stage: the reel with the result under it, centered in the space that is left.
    int stageTop = filtersBottom + S(24), stageBottom = g_L.panel.bottom;
    g_L.stage = { g_L.panel.left, stageTop, g_L.panel.right, stageBottom };
    int infoH = S(128), between = S(20), frame = S(18);
    int stageH = stageBottom - stageTop;
    g_L.reelCardH = std::clamp(stageH - infoH - between - 2 * frame, S(120), S(340));
    g_L.reelCardW = g_L.reelCardH * 2 / 3;
    int blockH = g_L.reelCardH + 2 * frame + between + infoH;
    int blockTop = stageTop + std::max(0, (stageH - blockH) / 2);
    g_L.reel = { g_L.panel.left, blockTop, g_L.panel.right, blockTop + g_L.reelCardH + 2 * frame };
    g_L.centerX = (g_L.panel.left + g_L.panel.right) / 2;
    g_L.centerY = blockTop + frame + g_L.reelCardH / 2;

    int infoTop = g_L.reel.bottom + between;
    g_L.name = { left, infoTop, right, infoTop + S(38) };
    g_L.stats = { left, infoTop + S(40), right, infoTop + S(62) };
    int buttonTop = infoTop + S(78), buttonH = S(46), cx = g_L.centerX;
    g_L.act[ACT_ROLL] = { cx - S(130), buttonTop, cx + S(130), buttonTop + buttonH };
    g_L.act[ACT_START] = { cx - S(200), buttonTop, cx - S(6), buttonTop + buttonH };
    g_L.act[ACT_AGAIN] = { cx + S(6), buttonTop, cx + S(200), buttonTop + buttonH };
}

}  // namespace

RECT CenterCard() {
    int w = (int)(g_L.reelCardW * kCenterScale), h = (int)(g_L.reelCardH * kCenterScale);
    return { g_L.centerX - w / 2, g_L.centerY - h / 2, g_L.centerX + w / 2, g_L.centerY + h / 2 };
}

void Relayout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // The top bar is shared: account, tabs, then the search field (library) or the actions (randomizer).
    int top = S(kTopBar);
    g_L.topBar = { 0, 0, W, top };
    int captionW = S(46), captionH = S(32);
    for (int i = 0; i < 3; ++i)
        g_L.caption[i] = { W - (3 - i) * captionW, 0, W - (2 - i) * captionW, captionH };

    int tabH = S(kTabHeight), tabW = S(kTabWidth), tabTop = (top - tabH) / 2, x = S(kTabsLeft);
    for (int i = 0; i < 2; ++i) {
        g_L.tab[i] = { x, tabTop, x + tabW, tabTop + tabH };
        x += tabW + S(kGap);
    }

    bool library = g_page == Page::Library;
    g_L.search = g_L.searchClear = {};
    for (RECT& button : g_L.btn) button = {};
    if (library) {
        int searchH = S(44), searchTop = (top - searchH) / 2, left = x + S(16);
        int width = std::clamp((int)g_L.caption[0].left - S(16) - left, S(180), S(520));
        g_L.search = { left, searchTop, left + width, searchTop + searchH };
        int clearSize = S(28);
        g_L.searchClear = { g_L.search.right - S(10) - clearSize, searchTop + (searchH - clearSize) / 2,
            g_L.search.right - S(10), searchTop + (searchH - clearSize) / 2 + clearSize };
        if (g_searchEdit) {
            int eh = S(18), editLeft = g_L.search.left + S(48);
            SetWindowPos(g_searchEdit, nullptr, editLeft, searchTop + (searchH - eh) / 2,
                g_L.searchClear.left - S(6) - editLeft, eh, SWP_NOZORDER);
        }
    } else {
        int bh = S(kTopButtonHeight), by = (top - bh) / 2, bx = g_L.caption[0].left - S(12);
        for (int i = BTN_COUNT - 1; i >= 0; --i) {
            int w = S(kButtonWidths[i]);
            g_L.btn[i] = { bx - w, by, bx, by + bh };
            bx -= w + S(kGap);
        }
    }
    if (g_searchEdit) ShowWindow(g_searchEdit, library ? SW_SHOW : SW_HIDE);

    if (library) LayoutLibrary(hwnd, W, H);
    else LayoutRandomizer(hwnd, W, H);

    if (g_toast.active) LayoutToast(hwnd);
    Redraw(hwnd);
}

// Switches the tab. A rename in progress and an open menu do not carry over to the other page.
void SetPage(HWND hwnd, Page page) {
    if (page == g_page) return;
    if (g_renameChip >= 0) {
        g_renameChip = -1;
        ShowWindow(g_renameEdit, SW_HIDE);
    }
    g_menu.open = false;
    g_drag = DragState{};
    if (GetCapture() == hwnd) ReleaseCapture();
    g_hover = Hover{};
    g_page = page;
    if (page == Page::Library) RefreshGames();
    SetFocus(hwnd);
    Relayout(hwnd);
}

}  // namespace playr
