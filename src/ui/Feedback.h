#pragma once

#include <string>

#include "ui/UiState.h"

namespace playr {

void LayoutToast(HWND hwnd);
void ShowToast(HWND hwnd, const std::wstring& text);

// Keeps the animation timer running while any button shows a badge.
void ShowBadge(HWND hwnd, int button, Badge::Mode mode, const std::wstring& text, ULONGLONG hideAfter = 0);
void TickBadges(HWND hwnd);

}  // namespace playr
