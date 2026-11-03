#include "ui/Categories.h"

#include <algorithm>

#include "model/Library.h"
#include "storage/Storage.h"
#include "ui/Feedback.h"
#include "ui/Layout.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

// Picking the current key again flips the direction; a new key starts with its natural one
// (alphabetical for names, largest or most recent first for the rest).
void SelectSort(HWND hwnd, SortKey key) {
    if (key == g_sortKey) {
        g_sortDescending = !g_sortDescending;
    } else {
        g_sortKey = key;
        g_sortDescending = key != SortKey::Name;
    }
    g_scroll = 0;
    RefreshGames();
    SaveSettings();
    Redraw(hwnd);
}

// Leaves rename mode. Focus only goes back to the window when the edit did not lose it to another control.
void EndRename(HWND hwnd, bool restoreFocus) {
    if (g_renameChip < 0) return;
    g_renameChip = -1;
    ShowWindow(g_renameEdit, SW_HIDE);
    if (restoreFocus) SetFocus(hwnd);
    Relayout(hwnd);
}

void StartRename(HWND hwnd, int chip) {
    g_renameChip = chip;
    SetWindowTextW(g_renameEdit, g_categories[chip].c_str());
    Relayout(hwnd);
    SetFocus(g_renameEdit);
    SendMessageW(g_renameEdit, EM_SETSEL, 0, -1);
}

void CommitRename(HWND hwnd) {
    if (g_renameChip < 0) return;
    int length = GetWindowTextLengthW(g_renameEdit);
    std::wstring name(length + 1, L'\0');
    GetWindowTextW(g_renameEdit, name.data(), length + 1);
    name.resize(length);
    const std::wstring old = g_categories[g_renameChip];

    if (name.empty() || name == old) { EndRename(hwnd, true); return; }
    if (name.find_first_of(L"\t\r\n") != std::wstring::npos) {
        ShowToast(hwnd, L"Der Name darf keine Tabs oder Zeilenumbrüche enthalten");
        return;
    }
    if (IsReservedCategory(name) || std::find(g_categories.begin(), g_categories.end(), name) != g_categories.end()) {
        ShowToast(hwnd, L"Diese Kategorie gibt es schon");
        return;
    }
    g_categories[g_renameChip] = name;
    for (auto& game : g_games)
        if (game.InCategory(old)) {
            game.RemoveCategory(old);
            game.AddCategory(name);
        }
    SaveSettings();
    EndRename(hwnd, true);
}

// "+ Erstellen" in the sidebar: adds a category with a default name and lets you type the real one right away.
void CreateCategoryRow(HWND hwnd) {
    std::wstring name = L"Neue Kategorie";
    for (int number = 2; std::find(g_categories.begin(), g_categories.end(), name) != g_categories.end(); ++number)
        name = L"Neue Kategorie " + std::to_wstring(number);
    g_categories.push_back(name);
    g_active = (int)g_categories.size() - 1;
    g_scroll = 0;
    g_sideScroll = MaxSideScroll() + S(kSideRow);
    RefreshGames();
    Relayout(hwnd);
    SaveSettings();
    StartRename(hwnd, g_active);
}

void DeleteCategory(HWND hwnd, int index) {
    const std::wstring name = g_categories[index];
    for (auto& game : g_games)
        game.RemoveCategory(name);
    g_categories.erase(g_categories.begin() + index);
    g_active = 0;
    g_scroll = 0;
    RefreshGames();
    Relayout(hwnd);
    SaveSettings();
}

void EndDrag(HWND hwnd) {
    if (GetCapture() == hwnd) ReleaseCapture();
    g_drag = DragState{};
    Redraw(hwnd);
}

void DropOnCategory(HWND hwnd) {
    if (g_drag.active && g_drag.dropChip >= 1 && g_drag.row >= 0 && g_drag.row < (int)g_visible.size()) {
        // Dropping copies: the game stays where it is and also joins the category.
        Game& game = g_games[g_visible[g_drag.row]];
        const std::wstring& category = g_categories[g_drag.dropChip];
        bool added = game.AddCategory(category);
        SaveSettings();
        EndDrag(hwnd);
        RefreshGames();
        ShowToast(hwnd, added ? L"Zu „" + category + L"“ hinzugefügt" : L"Schon in „" + category + L"“");
    } else {
        EndDrag(hwnd);
    }
}

}  // namespace playr
