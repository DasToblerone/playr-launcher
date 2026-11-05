#pragma once

#include "platform/Gdi.h"

namespace playr {

void DrawList(Gdiplus::Graphics& g, int W);
void DrawSidebar(Gdiplus::Graphics& g);
void DrawTopBar(Gdiplus::Graphics& g);
void DrawCaptionButtons(Gdiplus::Graphics& g);
void DrawSearchField(Gdiplus::Graphics& g);
void DrawUninstalledToggle(Gdiplus::Graphics& g);
void DrawSortButton(Gdiplus::Graphics& g);
void DrawBar(Gdiplus::Graphics& g, int W, int H);
void DrawActionButtons(Gdiplus::Graphics& g);  // the randomizer's scan and import buttons in the top bar
void DrawTabs(Gdiplus::Graphics& g);
void DrawFilters(Gdiplus::Graphics& g);
void DrawStage(Gdiplus::Graphics& g);
void DrawDragGhost(Gdiplus::Graphics& g);
void DrawMenu(Gdiplus::Graphics& g);
void DrawToast(Gdiplus::Graphics& g);

// Loads the Steam profile picture for the top bar.
void LoadAvatar();

// Paints the whole window through a back buffer.
void Paint(HWND hwnd);

}  // namespace playr
