#pragma once

#include <map>
#include <string>

#include "platform/Gdi.h"

namespace playr {

// Art scaled once to the size it is drawn at, with rounded corners, so painting only has to blit it.
// Scaling the full-size images on every repaint made hovering over many cards feel laggy.
struct BakedArt {
    int w = 0, h = 0;
    Gdiplus::Bitmap* bitmap = nullptr;
    bool gray = false;
};
using BakedCache = std::map<unsigned long long, BakedArt>;

extern BakedCache g_bakedRows, g_bakedCards, g_bakedTilesSmall, g_bakedTilesFull;

// The full-size art of a game, loaded lazily; null while the file is missing.
Gdiplus::Bitmap* GetCover(const std::wstring& appId);
Gdiplus::Bitmap* GetPortrait(const std::wstring& appId);

// Scales `source` to w x h with rounded corners; `crop` fills the box with the middle of the image and `gray`
// turns it into a dimmed black and white version, the look of games that are not installed.
Gdiplus::Bitmap* BakeArt(Gdiplus::Bitmap* source, int w, int h, Gdiplus::REAL radius, bool crop, bool gray = false);
Gdiplus::Bitmap* GetBaked(BakedCache& cache, const std::wstring& appId, Gdiplus::Bitmap* source, int w, int h,
    Gdiplus::REAL radius, bool crop, bool gray = false);

// A cover finished downloading (id carries kPortraitFlag for portrait art): drop what was cached for it and repaint.
void OnCoverDownloaded(HWND hwnd, unsigned long long id);
void ClearArt();

}  // namespace playr
