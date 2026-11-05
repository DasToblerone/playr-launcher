#pragma once

#include "platform/Win32.h"

namespace playr {

int CaptionButtonAt(POINT p);
int HitTab(POINT p);
int HitButton(POINT p);  // the scan and import buttons, and "Zufällig auswählen" in the library

// Library page.
int HitRow(POINT p);
int HitChip(POINT p);

// Randomizer page.
int HitRollChip(POINT p);
int HitAction(POINT p);  // the buttons under the reel, only those that can be clicked now
bool HitCard(POINT p);   // the card in the center, once a game has been drawn

// Updates what the pointer is over and repaints the parts that changed.
void UpdateHover(HWND hwnd, POINT p);

}  // namespace playr
