#include "ui/ContextMenu.h"

#include <algorithm>

#include "ui/HitTest.h"

using namespace Gdiplus;

namespace playr {

void CloseMenu(HWND hwnd) {
    if (!g_menu.open) return;
    g_menu = Menu{};
    g_hover.menuItem = -1;
    Redraw(hwnd);
}

void OpenMenu(HWND hwnd, POINT at, int chip, int game, std::initializer_list<Menu::Action> actions) {
    RECT client;
    GetClientRect(hwnd, &client);
    int w = S(game >= 0 ? kMenuWidth + 56 : kMenuWidth), pad = S(kMenuPad), item = S(kMenuItemHeight);
    int h = (int)actions.size() * item + 2 * pad;
    int x = std::min((int)at.x, (int)client.right - w - S(8)), y = std::min((int)at.y, (int)client.bottom - h - S(8));
    g_menu = Menu{};
    g_menu.open = true;
    g_menu.chip = chip;
    g_menu.game = game;
    g_menu.rect = { x, y, x + w, y + h };
    for (Menu::Action action : actions) {
        int i = g_menu.count++;
        g_menu.actions[i] = action;
        g_menu.items[i] = { x + pad, y + pad + i * item, x + w - pad, y + pad + (i + 1) * item };
    }
    UpdateHover(hwnd, at);
    Redraw(hwnd);
}

}  // namespace playr
