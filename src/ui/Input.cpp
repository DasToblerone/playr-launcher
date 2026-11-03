#include "ui/Input.h"

#include <algorithm>
#include <cstdlib>

#include "model/Library.h"
#include "storage/Storage.h"
#include "ui/Categories.h"
#include "ui/ContextMenu.h"
#include "ui/Format.h"
#include "ui/GameActions.h"
#include "ui/HitTest.h"
#include "ui/Layout.h"
#include "ui/MainWindow.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

void OnMouseDown(HWND hwnd, UINT msg, LPARAM lp) {
    SetFocus(hwnd);
    POINT p = ClientPoint(lp);
    if (!g_search.empty() && In(g_L.searchClear, p)) {
        SetWindowTextW(g_searchEdit, L"");
        return;
    }
    if (In(g_L.search, p)) {
        SetFocus(g_searchEdit);  // the whole field is clickable
        return;
    }
    if (g_menu.open) {
        // The menu swallows the click: pick an item, or just close it.
        Menu menu = g_menu;
        int picked = g_hover.menuItem;
        CloseMenu(hwnd);
        if (picked < 0 || picked >= menu.count) return;
        switch (menu.actions[picked]) {
        case Menu::Rename: StartRename(hwnd, menu.chip); break;
        case Menu::Delete: DeleteCategory(hwnd, menu.chip); break;
        case Menu::RemoveFromCategory:
            g_games[menu.game].RemoveCategory(g_categories[menu.chip]);
            SaveSettings();
            RefreshGames();
            Redraw(hwnd);
            break;
        default: SelectSort(hwnd, SortKeyOf(menu.actions[picked])); break;
        }
        return;
    }
    if (In(g_L.create, p)) {
        CreateCategoryRow(hwnd);
        return;
    }
    if (In(g_L.toggle, p)) {
        g_showUninstalled = !g_showUninstalled;
        g_scroll = 0;
        RefreshGames();
        SaveSettings();
        Redraw(hwnd);
        return;
    }
    if (In(g_L.sortBtn, p)) {
        POINT below{ g_L.sortBtn.right - S(kMenuWidth), g_L.sortBtn.bottom + S(4) };
        OpenMenu(hwnd, below, -1, -1, { Menu::SortByName, Menu::SortByPlaytime, Menu::SortByLastPlayed, Menu::SortBySize });
        return;
    }
    int chip = HitChip(p), button = HitButton(p), row = HitRow(p);
    if (chip >= 0) {
        g_active = chip;
        g_scroll = 0;
        RefreshGames();
        Redraw(hwnd);
    } else if (button >= 0) {
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_ACTION + button, BN_CLICKED), 0);
    } else if (row >= 0) {
        g_sel = g_visible[row];
        Redraw(hwnd);
        if (msg == WM_LBUTTONDBLCLK || InPlayZone(row, p)) {
            LaunchGame(hwnd, g_games[g_sel]);
        } else {
            g_drag.row = row;
            g_drag.start = p;
            SetCapture(hwnd);
        }
    }
}

void OnMouseMove(HWND hwnd, WPARAM wp, LPARAM lp) {
    if (!g_tracking) {
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&track);
        g_tracking = true;
    }
    POINT p = ClientPoint(lp);
    if (g_drag.row < 0 || !(wp & MK_LBUTTON)) { UpdateHover(hwnd, p); return; }

    if (!g_drag.active && (std::abs(p.x - g_drag.start.x) > S(6) || std::abs(p.y - g_drag.start.y) > S(6)))
        g_drag.active = true;
    if (g_drag.active) {
        g_drag.pos = p;
        int chip = HitChip(p);
        g_drag.dropChip = chip >= 1 ? chip : -1;
        Redraw(hwnd);
    }
}

void OnKeyDown(HWND hwnd, WPARAM key) {
    if (key == VK_F11) {
        ToggleFullscreen(hwnd);
        return;
    }
    if (key == 'F' && GetKeyState(VK_CONTROL) < 0) {  // Ctrl+F jumps to the search field
        SetFocus(g_searchEdit);
        return;
    }
    if (key == VK_ESCAPE) {
        if (g_menu.open) CloseMenu(hwnd);
        else if (g_fullscreen) ToggleFullscreen(hwnd);
        return;
    }
    bool vertical = key == VK_UP || key == VK_DOWN, horizontal = g_L.grid && (key == VK_LEFT || key == VK_RIGHT);
    if ((!vertical && !horizontal) || g_visible.empty()) return;

    // Up and down move by a row (several cards in the grid), left and right by one card.
    int step = vertical ? g_L.cols : 1;
    int current = -1;
    for (int i = 0; i < (int)g_visible.size(); ++i)
        if (g_visible[i] == g_sel) current = i;
    int direction = (key == VK_DOWN || key == VK_RIGHT) ? 1 : -1;
    int next = std::clamp(current < 0 ? 0 : current + direction * step, 0, (int)g_visible.size() - 1);
    g_sel = g_visible[next];
    EnsureVisible(next);
    Redraw(hwnd);
}

}  // namespace playr
