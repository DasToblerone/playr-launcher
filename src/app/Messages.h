#pragma once

#include "platform/Win32.h"

namespace playr {

constexpr UINT WM_COVER = WM_APP + 1;       // wParam: appId whose art finished downloading (+ kPortraitFlag)
constexpr unsigned long long kPortraitFlag = 1ull << 62;
constexpr UINT WM_SCANDONE = WM_APP + 2;    // lParam: heap-allocated std::map<std::wstring, Game>
constexpr UINT WM_CLASSIFIED = WM_APP + 3;  // wParam: appId, lParam: 1 when it is a game
constexpr UINT WM_OWNED = WM_APP + 4;       // lParam: heap-allocated OwnedResult

// The main window, which the background threads post their results to.
extern HWND g_mainWnd;

}  // namespace playr
