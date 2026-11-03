#include "ui/EditControls.h"

#include <string>

#include "ui/Categories.h"
#include "ui/Layout.h"
#include "ui/MainWindow.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

namespace {

LRESULT CALLBACK RenameEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == VK_F11) { ToggleFullscreen(GetParent(hwnd)); return 0; }
    if (msg == WM_CHAR) {
        if (wp == VK_RETURN) { CommitRename(GetParent(hwnd)); return 0; }
        if (wp == VK_ESCAPE) { EndRename(GetParent(hwnd), true); return 0; }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

}  // namespace

// The games shown follow what the search field holds.
void ApplySearch(HWND hwnd) {
    int length = GetWindowTextLengthW(g_searchEdit);
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(g_searchEdit, text.data(), length + 1);
    text.resize(length);
    g_search = text;
    g_scroll = 0;
    RefreshGames();
    Redraw(hwnd);
}

namespace {

LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == VK_F11) { ToggleFullscreen(GetParent(hwnd)); return 0; }
    if (msg == WM_CHAR && wp == VK_ESCAPE) {
        SetWindowTextW(hwnd, L"");  // Esc empties the field and gives the focus back
        SetFocus(GetParent(hwnd));
        return 0;
    }
    if (msg == WM_CHAR && wp == VK_RETURN) { SetFocus(GetParent(hwnd)); return 0; }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

}  // namespace

void CreateEditControls(HWND hwnd) {
    g_editFont = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    g_editBrush = CreateSolidBrush(RGB(31, 31, 31));
    g_searchEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 100, 20, hwnd, (HMENU)(INT_PTR)ID_SEARCH_EDIT, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_searchEdit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
    SendMessageW(g_searchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Spiele suchen");  // stays visible while focused
    SetWindowSubclass(g_searchEdit, SearchEditProc, 3, 0);

    g_renameEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
        0, 0, 100, 20, hwnd, (HMENU)(INT_PTR)ID_RENAME_EDIT, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_renameEdit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
    SetWindowSubclass(g_renameEdit, RenameEditProc, 2, 0);
}

void DestroyEditControls() {
    if (g_editFont) DeleteObject(g_editFont);
    if (g_editBrush) DeleteObject(g_editBrush);
}

}  // namespace playr
