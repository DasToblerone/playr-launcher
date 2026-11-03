#include "ui/ArtCache.h"

#include <cstdlib>
#include <set>

#include "app/Messages.h"
#include "storage/Storage.h"
#include "ui/DrawPrimitives.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

namespace {

// Art is loaded lazily and kept; a missing file is only looked up once until its download finishes.
struct ArtCache {
    std::map<unsigned long long, Bitmap*> bitmaps;
    std::set<unsigned long long> tried;

    Bitmap* Get(const std::wstring& appId, const fs::path& path) {
        unsigned long long id = wcstoull(appId.c_str(), nullptr, 10);
        auto it = bitmaps.find(id);
        if (it != bitmaps.end()) return it->second;
        if (!tried.insert(id).second) return nullptr;

        std::error_code ec;
        if (!fs::exists(path, ec)) return nullptr;
        Bitmap* bitmap = Bitmap::FromFile(path.c_str());
        if (!bitmap || bitmap->GetLastStatus() != Ok) { delete bitmap; return nullptr; }
        bitmaps[id] = bitmap;
        return bitmap;
    }

    void Forget(unsigned long long id) {
        auto it = bitmaps.find(id);
        if (it != bitmaps.end()) { delete it->second; bitmaps.erase(it); }
        tried.erase(id);
    }

    void Clear() {
        for (auto& entry : bitmaps) delete entry.second;
        bitmaps.clear();
    }
};

ArtCache g_headers, g_portraits;

}  // namespace

Bitmap* GetCover(const std::wstring& appId) { return g_headers.Get(appId, CoverPath(appId)); }
Bitmap* GetPortrait(const std::wstring& appId) { return g_portraits.Get(appId, PortraitPath(appId)); }

BakedCache g_bakedRows, g_bakedCards, g_bakedTilesSmall, g_bakedTilesFull;

namespace {

void ForgetBaked(unsigned long long id) {
    for (BakedCache* cache : { &g_bakedRows, &g_bakedCards, &g_bakedTilesSmall, &g_bakedTilesFull }) {
        auto it = cache->find(id);
        if (it == cache->end()) continue;
        delete it->second.bitmap;
        cache->erase(it);
    }
}

void ClearBaked() {
    for (BakedCache* cache : { &g_bakedRows, &g_bakedCards, &g_bakedTilesSmall, &g_bakedTilesFull }) {
        for (auto& entry : *cache) delete entry.second.bitmap;
        cache->clear();
    }
}

}  // namespace

void OnCoverDownloaded(HWND hwnd, unsigned long long id) {
    unsigned long long appId = id & ~kPortraitFlag;
    if (id & kPortraitFlag) g_portraits.Forget(appId);
    else g_headers.Forget(appId);
    ForgetBaked(appId);
    Redraw(hwnd);
}

// Scales `source` to w x h with rounded corners; `crop` fills the box with the middle of the image and `gray`
// turns it into a dimmed black and white version, the look of games that are not installed.
Bitmap* BakeArt(Bitmap* source, int w, int h, REAL radius, bool crop, bool gray) {
    Bitmap* baked = new Bitmap(w, h, PixelFormat32bppPARGB);
    Graphics g(baked);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    GraphicsPath clip;
    RoundPath(clip, RectF(0, 0, (REAL)w, (REAL)h), radius);
    g.SetClip(&clip);

    ImageAttributes attributes;
    if (gray) {
        const REAL r = 0.30f * 0.8f, gr = 0.59f * 0.8f, b = 0.11f * 0.8f;  // luminance, a bit darker
        ColorMatrix matrix = { r, r, r, 0, 0,
                               gr, gr, gr, 0, 0,
                               b, b, b, 0, 0,
                               0, 0, 0, 1, 0,
                               0, 0, 0, 0, 1 };
        attributes.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    }
    RectF dest(0, 0, (REAL)w, (REAL)h);
    REAL sourceW = (REAL)source->GetWidth(), sourceH = (REAL)source->GetHeight(), sourceX = 0;
    if (crop) {
        sourceW = sourceH * w / h;
        sourceX = ((REAL)source->GetWidth() - sourceW) / 2;
    }
    g.DrawImage(source, dest, sourceX, 0, sourceW, sourceH, UnitPixel, &attributes);
    return baked;
}

Bitmap* GetBaked(BakedCache& cache, const std::wstring& appId, Bitmap* source, int w, int h, REAL radius, bool crop,
    bool gray) {
    BakedArt& entry = cache[wcstoull(appId.c_str(), nullptr, 10)];
    if (!entry.bitmap || entry.w != w || entry.h != h || entry.gray != gray) {
        delete entry.bitmap;
        entry = { w, h, BakeArt(source, w, h, radius, crop, gray), gray };
    }
    return entry.bitmap;
}

void ClearArt() {
    g_headers.Clear();
    g_portraits.Clear();
    ClearBaked();
}

}  // namespace playr
