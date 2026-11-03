#pragma once

#include "platform/Win32.h"

namespace playr {

void OnMouseDown(HWND hwnd, UINT msg, LPARAM lp);
void OnMouseMove(HWND hwnd, WPARAM wp, LPARAM lp);
void OnKeyDown(HWND hwnd, WPARAM key);

}  // namespace playr
