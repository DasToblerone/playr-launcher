#pragma once

#include <string>
#include <vector>

#include "platform/Gdi.h"
#include "ui/Theme.h"

namespace playr {

struct Layout {
    RECT list{};
    RECT bar{};
    RECT btn[BTN_COUNT]{};
    RECT sidebar{}, main{}, sideList{}, create{};  // create: "+ Erstellen" in the sidebar header
    RECT topBar{};
    RECT search{}, searchClear{};  // the search field in the top bar and its "x"
    RECT caption[3]{};             // minimize, maximize and close buttons at the top right
    RECT sortBtn{};                          // "Sortieren" at the top right of the main panel
    RECT toggle{};                           // switch for the games that are not installed, left of it
    std::vector<RECT> chips;  // one row of the sidebar per category
    int rowH = 0;  // vertical step between rows of items (list rows or card rows)
    int wheelStep = 0;
    bool grid = false;
    int cols = 1, cellW = 0, cellH = 0, cardGap = 0, cardPad = 0, artW = 0, artH = 0;
};

struct Hover {
    int row = -1, button = -1, chip = -1;
    bool cover = false;
    int menuItem = -1;
    bool sideCreate = false;
    bool sortBtn = false;
    bool searchClear = false;
    bool toggle = false;
    bool Any() const {
        return row >= 0 || button >= 0 || chip >= 0 || menuItem >= 0 || sideCreate || sortBtn || searchClear || toggle;
    }
};

// Context menu, opened with the right mouse button on a category row or on a game.
struct Menu {
    enum Action { Rename, Delete, RemoveFromCategory, SortByName, SortByPlaytime, SortByLastPlayed, SortBySize };
    static constexpr int kMaxItems = 4;
    bool open = false;
    int chip = -1;  // the category it belongs to
    int game = -1;  // index into g_games for a game menu
    int count = 0;
    Action actions[kMaxItems]{};
    RECT rect{};
    RECT items[kMaxItems]{};
};

// A short message above the bottom bar.
struct Toast {
    bool active = false;
    std::wstring text;
    ULONGLONG since = 0, duration = 0;
    RECT rect{};
};

struct DragState {
    int row = -1;            // visible index of the pressed row
    bool active = false;     // true once the pointer moved far enough to count as a drag
    POINT start{}, pos{};
    int dropChip = -1;
};

// Short feedback inside a button: a spinner, an animated check mark, or plain text.
struct Badge {
    enum Mode { Off, Spinner, Check, Text } mode = Off;
    std::wstring text;
    ULONGLONG since = 0;
    ULONGLONG hideAfter = 0;  // ms, for Check and Text
};

extern Gdiplus::Bitmap* g_avatar;        // the Steam profile picture
extern Gdiplus::Bitmap* g_avatarBaked;   // it, round and scaled for the top bar
extern int g_avatarSize;

extern float g_scale;
extern Layout g_L;
extern Hover g_hover;
extern DragState g_drag;
extern Badge g_badge[BTN_COUNT];
extern Toast g_toast;
extern Badge g_importResult;
extern bool g_importReady;
extern Menu g_menu;
extern int g_renameChip;   // category chip being edited in place
extern HWND g_renameEdit;
extern HWND g_searchEdit;
extern std::wstring g_search;    // what the search field currently holds
extern bool g_scanReady;
extern bool g_tracking;
extern bool g_fullscreen;
extern int g_captionHover, g_captionPressed;  // minimize, maximize, close
extern HFONT g_editFont;
extern HBRUSH g_editBrush;
extern std::vector<int> g_visible;  // indices into g_games for the current category
extern int g_active;                // selected category chip
extern int g_sel;                   // selected game (index into g_games)
extern int g_scroll;
extern int g_sideScroll;            // scroll position of the sidebar

// Scales a size in 96-dpi pixels to the current display.
int S(float v);
Gdiplus::RectF RF(const RECT& r);
bool In(const RECT& r, POINT p);
POINT ClientPoint(LPARAM lp);
void Redraw(HWND hwnd);

const std::wstring& SelectedCategory();
int ContentHeight();
int MaxScroll();
void ClampScroll();
int MaxSideScroll();
void ClampSideScroll();

// The field that replaces a category's name while it is renamed.
RECT RenameRect(int chip);

}  // namespace playr
