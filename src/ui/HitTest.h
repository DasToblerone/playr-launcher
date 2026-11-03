#pragma once

#include "platform/Win32.h"

namespace playr {

int HitRow(POINT p);
int HitButton(POINT p);
int HitChip(POINT p);
int CaptionButtonAt(POINT p);

// Updates what the pointer is over and repaints only the parts that changed.
void UpdateHover(HWND hwnd, POINT p);

}  // namespace playr
