#include "ui/Draw.h"

#include <algorithm>

#include "model/Library.h"
#include "ui/ArtCache.h"
#include "ui/DrawPrimitives.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

namespace {

// The tile of a category: its first games as a mosaic, like a playlist cover.
void DrawCategoryTile(Graphics& g, int category, RectF tile) {
    GraphicsPath shape;
    RoundPath(shape, tile, (REAL)S(6));
    GraphicsState state = g.Save();
    g.SetClip(&shape, CombineModeIntersect);

    std::vector<const Game*> games;
    for (const auto& game : g_games) {
        if (category == 0 || game.InCategory(g_categories[category])) games.push_back(&game);
        if (games.size() == 4) break;
    }
    REAL cx = tile.X + tile.Width / 2, cy = tile.Y + tile.Height / 2;
    if (category == 0) {
        SolidBrush brush(kAccent);
        g.FillRectangle(&brush, tile);
        DrawIcon(g, Icon::Library, cx, cy, (REAL)S(22), kOnAccent);
    } else if (games.empty()) {
        SolidBrush brush(kSurface);
        g.FillRectangle(&brush, tile);
        DrawIcon(g, Icon::Folder, cx, cy, (REAL)S(24), kText2);
    } else {
        int cells = games.size() == 1 ? 1 : 2;
        REAL cell = tile.Width / cells;
        SolidBrush empty(kCover);
        g.FillRectangle(&empty, tile);
        for (int i = 0; i < (int)games.size() && i < cells * cells; ++i) {
            Bitmap* header = GetCover(games[i]->appId);
            if (!header) continue;
            BakedCache& cache = cells == 1 ? g_bakedTilesFull : g_bakedTilesSmall;
            int size = (int)cell;
            Bitmap* baked = GetBaked(cache, games[i]->appId, header, size, size, 1.0f, true, !games[i]->installed);
            g.DrawImage(baked, Rect((int)(tile.X + (i % cells) * cell), (int)(tile.Y + (i / cells) * cell), size, size));
        }
    }
    g.Restore(state);
}

}  // namespace

// The library: "Alle Spiele" and the categories as rows, like playlists in Spotify's sidebar.
void DrawSidebar(Graphics& g) {
    RectF panel = RF(g_L.sidebar);
    FillRound(g, panel, (REAL)S(8), kBg);
    DrawLabel(g, L"Bibliothek", (REAL)S(17), true, kText,
        RectF(panel.X + S(20), panel.Y, (REAL)S(140), (REAL)S(kSideHeader)), StringAlignmentNear, StringAlignmentCenter);
    RectF create = RF(g_L.create);
    FillRound(g, create, create.Height / 2, g_hover.sideCreate ? kChipHover : kSurface);
    DrawButtonContent(g, create, Icon::Plus, L"Erstellen", kText);

    GraphicsState state = g.Save();
    g.SetClip(RF(g_L.sideList), CombineModeIntersect);
    for (int i = 0; i < (int)g_L.chips.size(); ++i) {
        if (g_L.chips[i].bottom < g_L.sideList.top || g_L.chips[i].top > g_L.sideList.bottom) continue;
        RectF r = RF(g_L.chips[i]);
        bool dragging = g_drag.active;
        bool target = dragging && i >= 1 && i == g_drag.dropChip;
        bool active = i == g_active;
        bool hovered = !dragging && i == g_hover.chip;

        if (target) {
            FillRound(g, r, (REAL)S(8), Color(60, 168, 85, 247));
            StrokeRound(g, r, (REAL)S(8), kAccent, 2.0f);
        } else if (dragging && i >= 1) {
            StrokeRound(g, r, (REAL)S(8), kAccent, 1.5f, true);
        } else if (active) {
            FillRound(g, r, (REAL)S(8), kSurface);  // same colors as the game cards: selected is lighter than hovered
        } else if (hovered) {
            FillRound(g, r, (REAL)S(8), kSurfaceHover);
        }

        REAL tileSize = (REAL)S(kSideTile);
        RectF tile(r.X + S(8), r.Y + (r.Height - tileSize) / 2, tileSize, tileSize);
        DrawCategoryTile(g, i, tile);

        REAL tx = tile.GetRight() + S(12), tw = r.GetRight() - tx - S(8);
        if (i == g_renameChip) {  // the edit control sits on top of the name
            RectF field = RF(RenameRect(i));
            FillRound(g, field, (REAL)S(6), kField);
            StrokeRound(g, field, (REAL)S(6), kText, 2.0f);
        } else {
            Color name = active && !dragging ? kAccentHover : (dragging && i == 0 ? kOutline : kText);
            DrawLabel(g, g_categories[i], (REAL)S(15), true, name, RectF(tx, r.Y + S(12), tw, (REAL)S(22)));
        }
        int count = i == 0 ? (int)g_games.size()
            : (int)std::count_if(g_games.begin(), g_games.end(), [&](const Game& game) { return game.InCategory(g_categories[i]); });
        DrawLabel(g, std::to_wstring(count) + (count == 1 ? L" Spiel" : L" Spiele"), (REAL)S(13), false, kText2,
            RectF(tx, r.Y + S(36), tw, (REAL)S(18)));
    }
    g.Restore(state);
}

}  // namespace playr
