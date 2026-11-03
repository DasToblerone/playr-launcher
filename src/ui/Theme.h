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
constexpr float kRandomWidth = 190;
constexpr float kMinClientWidth = 830, kMinClientHeight = 520;
constexpr float kStartClientWidth = 1120, kStartClientHeight = 680;

constexpr int ID_RENAME_EDIT = 2002;
constexpr int ID_SEARCH_EDIT = 2003;
constexpr int ID_ACTION = 3000;  // + button index
constexpr UINT_PTR TIMER_ANIMATION = 1;
// A popup window with a resizable frame and no caption: it draws its own title bar. (An overlapped window would get
// a caption from the system whether it asks for one or not.)
constexpr DWORD FRAME_STYLE = WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
constexpr DWORD WINDOW_STYLE = WS_POPUP | FRAME_STYLE | WS_CLIPCHILDREN;

// A main panel at least this wide switches from a list to a grid of cards.
constexpr float kGridMinWidth = 820, kCardPad = 12, kCardGap = 8, kCardTargetWidth = 200, kCardTextHeight = 74;


enum { BTN_SCAN, BTN_IMPORT, BTN_RANDOM, BTN_COUNT };
enum class Icon { None, Refresh, Download, Plus, Library, Folder, Sort, Search };
const wchar_t* const kButtonLabels[BTN_COUNT] = { L"Neu scannen", L"Kategorien aus Steam", L"Zufällig auswählen" };
const Icon kButtonIcons[BTN_COUNT] = { Icon::Refresh, Icon::Download, Icon::None };
const float kButtonWidths[BTN_COUNT - 1] = { 140, 200 };

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
