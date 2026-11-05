#pragma once

#include "platform/Win32.h"
#include "ui/LibraryLayout.h"
#include "ui/Theme.h"

namespace playr {

// Positions everything for the current window size and page: the top bar, then the page below it.
void Relayout(HWND hwnd);

// Opens a tab.
void SetPage(HWND hwnd, Page page);

// The slot of the card in the center of the reel, a bit larger than the other cards.
RECT CenterCard();

}  // namespace playr
