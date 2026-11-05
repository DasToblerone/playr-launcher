#pragma once

#include "platform/Gdi.h"

namespace playr {

// Sizes are in 96-dpi pixels and go through S() before use.
constexpr float kMargin = 24, kRowHeight = 64, kRowInset = 14, kNumberColumn = 52;
constexpr float kCoverWidth = 96, kCoverHeight = 45;
constexpr float kPanelGap = 8, kSidebarWidth = 300, kSideHeader = 56, kSideRow = 64, kSideTile = 48;
constexpr float kMainHeader = 82, kTopBar = 60;
constexpr float kMenuWidth = 176, kMenuItemHeight = 38, kMenuPad = 6;
constexpr float kBarHeight = 76, kControlHeight = 40, kBarMargin = 20, kGap = 8;
constexpr float kMinClientWidth = 900, kMinClientHeight = 560;
constexpr float kStartClientWidth = 1120, kStartClientHeight = 700;

// The top bar: the account on the left, then the tabs, then the search field (library) or the actions (randomizer).
constexpr float kTabHeight = 34, kTabWidth = 112, kTabsLeft = 250, kTopButtonHeight = 34;

// The randomizer page.
constexpr float kPanelPad = 28;
constexpr float kChipHeight = 34, kChipGap = 8, kLabelWidth = 96, kChipMaxWidth = 220;

constexpr int ID_RENAME_EDIT = 2002;
constexpr int ID_SEARCH_EDIT = 2003;
constexpr int ID_ACTION = 3000;  // + button index
constexpr UINT_PTR TIMER_ANIMATION = 1;  // toast and button badges
constexpr UINT_PTR TIMER_SPIN = 2;       // the reel
// A popup window with a resizable frame and no caption: it draws its own title bar. (An overlapped window would get
// a caption from the system whether it asks for one or not.)
constexpr DWORD FRAME_STYLE = WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
constexpr DWORD WINDOW_STYLE = WS_POPUP | FRAME_STYLE | WS_CLIPCHILDREN;

// A main panel at least this wide switches from a list to a grid of cards.
constexpr float kGridMinWidth = 820, kCardPad = 12, kCardGap = 8, kCardTargetWidth = 200, kCardTextHeight = 74;

// The two pages of the window.
enum class Page { Randomizer, Library };
const wchar_t* const kTabLabels[2] = { L"Würfeln", L"Bibliothek" };

// Buttons with a badge: scan and import, in the bottom bar of the library and in the top bar of the randomizer.
enum { BTN_SCAN, BTN_IMPORT, BTN_COUNT };
enum { ACT_ROLL, ACT_START, ACT_AGAIN, ACT_COUNT };  // the buttons under the reel
enum class Icon { None, Refresh, Download, Plus, Library, Folder, Sort, Search, Dice, Play };
const wchar_t* const kButtonLabels[BTN_COUNT] = { L"Neu scannen", L"Steam-Sammlungen" };
const Icon kButtonIcons[BTN_COUNT] = { Icon::Refresh, Icon::Download };
const float kButtonWidths[BTN_COUNT] = { 140, 200 };

inline const Gdiplus::Color kBg(255, 18, 18, 18);
inline const Gdiplus::Color kBar(255, 0, 0, 0);
inline const Gdiplus::Color kSurface(255, 42, 42, 42);
inline const Gdiplus::Color kField(255, 31, 31, 31);
inline const Gdiplus::Color kSurfaceHover(255, 31, 31, 31);
inline const Gdiplus::Color kChipHover(255, 62, 62, 62);
inline const Gdiplus::Color kCover(255, 40, 40, 40);
inline const Gdiplus::Color kAccent(255, 168, 85, 247);
inline const Gdiplus::Color kAccentHover(255, 192, 132, 252);
inline const Gdiplus::Color kText(255, 255, 255, 255);
inline const Gdiplus::Color kText2(255, 179, 179, 179);
inline const Gdiplus::Color kLine(255, 31, 31, 31);
inline const Gdiplus::Color kOutline(255, 114, 114, 114);
inline const Gdiplus::Color kOnAccent(255, 0, 0, 0);
inline const Gdiplus::Color kDanger(255, 229, 72, 77);
inline const Gdiplus::Color kDangerHover(255, 240, 98, 102);

}  // namespace playr
