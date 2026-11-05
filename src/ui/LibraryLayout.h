#pragma once

#include "platform/Win32.h"

namespace playr {

void RefreshGames();  // rebuilds the visible list from category, search and sort
// Lays out the library page (sidebar, games, bottom bar) for a client area of W x H.
void LayoutLibrary(HWND hwnd, int W, int H);

// Geometry of item `i` (visible index): a list row, or a card in the grid.
int ItemTop(int i);
RECT ItemCell(int i);
RECT ArtRect(int i);

// The area that starts the game when clicked: the whole cover in the list, the round button on a card.
RECT PlayRect(int i);
bool InPlayZone(int i, POINT p);

void EnsureVisible(int visibleIndex);

// Places the category rows of the sidebar for the current scroll position.
void LayoutSideRows();

}  // namespace playr
