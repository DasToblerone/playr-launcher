#include "ui/UiState.h"

#include <algorithm>
#include <cmath>

#include "model/Library.h"

using namespace Gdiplus;

namespace playr {

Bitmap* g_avatar = nullptr;        // the Steam profile picture
Bitmap* g_avatarBaked = nullptr;   // it, round and scaled for the top bar
int g_avatarSize = 0;

float g_scale = 1.0f;
Layout g_L;
Hover g_hover;
DragState g_drag;
Badge g_badge[BTN_COUNT];
Toast g_toast;
Badge g_importResult;
bool g_importReady = false;
Menu g_menu;
int g_renameChip = -1;   // category chip being edited in place
HWND g_renameEdit = nullptr;
HWND g_searchEdit = nullptr;
std::wstring g_search;    // what the search field currently holds
bool g_scanReady = false;
bool g_tracking = false;
bool g_fullscreen = false;
int g_captionHover = -1, g_captionPressed = -1;  // minimize, maximize, close
HFONT g_editFont = nullptr;
HBRUSH g_editBrush = nullptr;
std::vector<int> g_visible;  // indices into g_games for the current category
int g_active = 0;            // selected category chip
int g_sel = -1;              // selected game (index into g_games)
int g_scroll = 0;
int g_sideScroll = 0;        // scroll position of the sidebar

int S(float v) { return (int)std::lround(v * g_scale); }

RectF RF(const RECT& r) {
    return RectF((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
}

bool In(const RECT& r, POINT p) { return PtInRect(&r, p) != 0; }

const std::wstring& SelectedCategory() {
    return g_active >= 0 && g_active < (int)g_categories.size() ? g_categories[g_active] : kAllGames;
}

int ContentHeight() {
    int rows = ((int)g_visible.size() + g_L.cols - 1) / g_L.cols;
    return rows * g_L.rowH;
}

int MaxScroll() {
    int view = (int)(g_L.list.bottom - g_L.list.top);
    return std::max(0, ContentHeight() - view);
}

void ClampScroll() { g_scroll = std::clamp(g_scroll, 0, MaxScroll()); }

int MaxSideScroll() {
    int content = (int)g_categories.size() * S(kSideRow), view = (int)(g_L.sideList.bottom - g_L.sideList.top);
    return std::max(0, content - view);
}

void ClampSideScroll() { g_sideScroll = std::clamp(g_sideScroll, 0, MaxSideScroll()); }

// The field that replaces a category's name while it is renamed.
RECT RenameRect(int chip) {
    const RECT& row = g_L.chips[chip];
    int left = row.left + S(8) + S(kSideTile) + S(12) - S(10), top = row.top + S(8);
    return { left, top, row.right - S(6), top + S(28) };
}

void Redraw(HWND hwnd) { InvalidateRect(hwnd, nullptr, FALSE); }

POINT ClientPoint(LPARAM lp) { return { (short)LOWORD(lp), (short)HIWORD(lp) }; }

}  // namespace playr
