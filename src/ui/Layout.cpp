#include "ui/Layout.h"

#include <algorithm>
#include <cmath>

#include "model/Library.h"
#include "ui/Feedback.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

namespace {

// Case-insensitive "contains", for the search field.
bool NameContains(const std::wstring& name, const std::wstring& needle) {
    auto lower = [](std::wstring text) {
        if (!text.empty()) CharLowerBuffW(text.data(), (DWORD)text.size());
        return text;
    };
    return lower(name).find(lower(needle)) != std::wstring::npos;
}

}  // namespace

void RefreshGames() {
    const std::wstring& filter = SelectedCategory();
    g_visible.clear();
    bool selectionVisible = false;
    for (int i = 0; i < (int)g_games.size(); ++i) {
        if (filter != kAllGames && !g_games[i].InCategory(filter)) continue;
        if (!g_showUninstalled && !g_games[i].installed) continue;
        if (!g_search.empty() && !NameContains(g_games[i].name, g_search)) continue;
        g_visible.push_back(i);
        selectionVisible |= i == g_sel;
    }
    // Numbers sort largest first by default; ties and names fall back to the alphabet.
    std::stable_sort(g_visible.begin(), g_visible.end(), [](int a, int b) {
        const Game& x = g_games[a];
        const Game& y = g_games[b];
        long long keyX = 0, keyY = 0;
        switch (g_sortKey) {
        case SortKey::Playtime: keyX = x.playtimeMinutes; keyY = y.playtimeMinutes; break;
        case SortKey::LastPlayed: keyX = x.lastPlayed; keyY = y.lastPlayed; break;
        case SortKey::Size: keyX = x.sizeBytes; keyY = y.sizeBytes; break;
        case SortKey::Name: break;
        }
        if (keyX != keyY) return g_sortDescending ? keyX > keyY : keyX < keyY;
        int byName = _wcsicmp(x.name.c_str(), y.name.c_str());
        return g_sortKey == SortKey::Name && g_sortDescending ? byName > 0 : byName < 0;
    });
    if (!selectionVisible) g_sel = -1;
    ClampScroll();
}

void Relayout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // Two rounded panels on a black window, like Spotify: the library on the left, the games on the right.
    int gap = S(kPanelGap), panelBottom = H - S(kBarHeight);
    g_L.bar = { 0, panelBottom, W, H };
    int top = S(kTopBar);
    g_L.topBar = { 0, 0, W, top };
    int captionW = S(46), captionH = S(32);
    for (int i = 0; i < 3; ++i)
        g_L.caption[i] = { W - (3 - i) * captionW, 0, W - (2 - i) * captionW, captionH };
    // The search field sits in the middle of the top bar, like Spotify's.
    int searchW = std::clamp(W - 2 * S(300), S(220), S(520)), searchH = S(44), searchTop = (top - searchH) / 2;
    g_L.search = { (W - searchW) / 2, searchTop, (W + searchW) / 2, searchTop + searchH };
    int clearSize = S(28);
    g_L.searchClear = { g_L.search.right - S(10) - clearSize, searchTop + (searchH - clearSize) / 2,
        g_L.search.right - S(10), searchTop + (searchH - clearSize) / 2 + clearSize };
    if (g_searchEdit) {
        int eh = S(18), left = g_L.search.left + S(48);
        SetWindowPos(g_searchEdit, nullptr, left, searchTop + (searchH - eh) / 2,
            g_L.searchClear.left - S(6) - left, eh, SWP_NOZORDER);
    }
    g_L.sidebar = { gap, top, gap + S(kSidebarWidth), std::max(top + S(100), panelBottom - S(4)) };
    g_L.main = { g_L.sidebar.right + gap, top, W - gap, g_L.sidebar.bottom };
    g_L.sideList = { g_L.sidebar.left, g_L.sidebar.top + S(kSideHeader), g_L.sidebar.right, g_L.sidebar.bottom - S(8) };
    int createW = S(104), createH = S(34);
    g_L.create = { g_L.sidebar.right - S(12) - createW, g_L.sidebar.top + (S(kSideHeader) - createH) / 2,
        g_L.sidebar.right - S(12), g_L.sidebar.top + (S(kSideHeader) - createH) / 2 + createH };
    int sortW = S(200), sortH = S(34);
    g_L.sortBtn = { g_L.main.right - S(kMargin) - sortW, g_L.main.top + S(24), g_L.main.right - S(kMargin),
        g_L.main.top + S(24) + sortH };
    // In a narrow panel the switch loses its label to leave room for the category title.
    int toggleW = g_L.main.right - g_L.main.left < S(760) ? S(48) : S(176);
    g_L.toggle = { g_L.sortBtn.left - S(16) - toggleW, g_L.sortBtn.top, g_L.sortBtn.left - S(16), g_L.sortBtn.bottom };
    int listTop = g_L.main.top + S(kMainHeader);
    g_L.list = { g_L.main.left, listTop, g_L.main.right, std::max(listTop, (int)g_L.main.bottom - S(8)) };

    int mainWidth = g_L.main.right - g_L.main.left;
    g_L.rowH = S(kRowHeight);
    g_L.cols = 1;
    g_L.grid = mainWidth >= S(kGridMinWidth);
    if (g_L.grid) {
        // Portrait art in a grid; the cells stretch so the columns fill the panel.
        g_L.cardPad = S(kCardPad);
        g_L.cardGap = S(kCardGap);
        int available = mainWidth - 2 * (S(kMargin) - g_L.cardPad);
        g_L.cols = std::max(1, (available + g_L.cardGap) / (S(kCardTargetWidth) + g_L.cardGap));
        g_L.cellW = (available - g_L.cardGap * (g_L.cols - 1)) / g_L.cols;
        g_L.artW = g_L.cellW - 2 * g_L.cardPad;
        g_L.artH = g_L.artW * 3 / 2;
        g_L.cellH = g_L.cardPad + g_L.artH + S(10) + S(kCardTextHeight) + g_L.cardPad;
        g_L.rowH = g_L.cellH + g_L.cardGap;
    }
    g_L.wheelStep = g_L.grid ? g_L.rowH / 3 : g_L.rowH;
    // The categories are rows of the sidebar, which scrolls on its own when there are many.
    ClampSideScroll();
    g_L.chips.clear();
    for (int i = 0; i < (int)g_categories.size(); ++i) {
        int top = g_L.sideList.top + i * S(kSideRow) - g_sideScroll;
        g_L.chips.push_back({ g_L.sidebar.left + S(8), top, g_L.sidebar.right - S(8), top + S(kSideRow) });
    }

    int ph = S(kControlHeight);
    int py = g_L.bar.top + (S(kBarHeight) - ph) / 2;
    // The buttons live in a column of at most 1300 pixels that stays centered, so they are easy to find in a big window.
    int margin = std::max(S(kBarMargin), (W - S(1300)) / 2);
    int x = margin;
    for (int i = 0; i < BTN_RANDOM; ++i) {
        g_L.btn[i] = { x, py, x + S(kButtonWidths[i]), py + ph };
        x += S(kButtonWidths[i]) + S(kGap);
    }
    g_L.btn[BTN_RANDOM] = { W - margin - S(kRandomWidth), py, W - margin, py + ph };

    if (g_renameEdit && g_renameChip >= 0 && g_renameChip < (int)g_L.chips.size()) {
        RECT field = RenameRect(g_renameChip);
        int eh = S(18);
        SetWindowPos(g_renameEdit, HWND_TOP, field.left + S(10), field.top + (field.bottom - field.top - eh) / 2,
            field.right - field.left - S(20), eh, SWP_SHOWWINDOW);
    }
    ClampScroll();
    ClampSideScroll();
    if (g_toast.active) LayoutToast(hwnd);
    Redraw(hwnd);
}

// Geometry of item `i` (visible index): a list row, or a card in the grid.
int ItemTop(int i) { return g_L.list.top + (i / g_L.cols) * g_L.rowH - g_scroll; }

RECT ItemCell(int i) {
    int top = ItemTop(i);
    if (!g_L.grid) return { (int)g_L.list.left, top, (int)g_L.list.right, top + g_L.rowH };
    int left = g_L.list.left + S(kMargin) - g_L.cardPad + (i % g_L.cols) * (g_L.cellW + g_L.cardGap);
    return { left, top, left + g_L.cellW, top + g_L.cellH };
}

RECT ArtRect(int i) {
    RECT cell = ItemCell(i);
    if (g_L.grid) {
        int left = cell.left + g_L.cardPad, top = cell.top + g_L.cardPad;
        return { left, top, left + g_L.artW, top + g_L.artH };
    }
    int left = cell.left + S(kRowInset) + S(kNumberColumn);
    int top = cell.top + S(2) + (g_L.rowH - S(4) - S(kCoverHeight)) / 2;
    return { left, top, left + S(kCoverWidth), top + S(kCoverHeight) };
}

// The area that starts the game when clicked: the whole cover in the list, the round button on a card.
RECT PlayRect(int i) {
    RECT art = ArtRect(i);
    if (!g_L.grid) return art;
    int d = S(44), inset = S(8);
    return { art.right - inset - d, art.bottom - inset - d, art.right - inset, art.bottom - inset };
}

bool InPlayZone(int i, POINT p) {
    if (i < 0 || !g_games[g_visible[i]].installed) return false;  // games that are not installed cannot be started
    RECT r = PlayRect(i);
    return In(r, p);
}

void EnsureVisible(int visibleIndex) {
    int top = (visibleIndex / g_L.cols) * g_L.rowH, view = g_L.list.bottom - g_L.list.top;
    if (top < g_scroll) g_scroll = top;
    else if (top + g_L.rowH > g_scroll + view) g_scroll = top + g_L.rowH - view;
    ClampScroll();
}

}  // namespace playr
