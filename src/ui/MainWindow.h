#pragma once

#include "platform/Win32.h"

namespace playr {

// Creates the main window; null when that fails.
HWND CreateMainWindow(HINSTANCE instance);

// F11: borderless fullscreen on the current monitor, and back to the previous window position.
void ToggleFullscreen(HWND hwnd);

}  // namespace playr
