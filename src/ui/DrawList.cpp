#include "ui/Draw.h"

#include <algorithm>

#include "model/Library.h"
#include "ui/ArtCache.h"
#include "ui/DrawPrimitives.h"
#include "ui/Format.h"
#include "ui/Layout.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

namespace {

// A thin bar: the share of achievements that is unlocked. A finished game gets the lighter accent.
void DrawProgress(Graphics& g, RectF bar, int done, int total) {
    FillRound(g, bar, bar.Height / 2, kSurface);
    if (done <= 0 || total <= 0) return;
    REAL share = std::min(1.0f, (REAL)done / (REAL)total);
    FillRound(g, RectF(bar.X, bar.Y, std::max(bar.Height, bar.Width * share), bar.Height), bar.Height / 2,
        done >= total ? kAccentHover : kAccent);
}

void DrawRow(Graphics& g, int visibleIndex, int W) {
    int rowTop = ItemTop(visibleIndex);
    const Game& game = g_games[g_visible[visibleIndex]];
    bool selected = g_visible[visibleIndex] == g_sel;
    bool hover = visibleIndex == g_hover.row;

    RECT cellRect = ItemCell(visibleIndex);
    RectF slot((REAL)cellRect.left + S(kRowInset), (REAL)rowTop + S(2),
        (REAL)(cellRect.right - cellRect.left - 2 * S(kRowInset)), (REAL)(g_L.rowH - S(4)));
    if (selected) FillRound(g, slot, (REAL)S(8), kSurface);
    else if (hover) FillRound(g, slot, (REAL)S(8), kSurfaceHover);

    DrawCentered(g, std::to_wstring(visibleIndex + 1), (REAL)S(15), false, selected ? kAccentHover : kText2,
        RectF(slot.X, slot.Y, (REAL)S(kNumberColumn), slot.Height));

    RectF cover = RF(ArtRect(visibleIndex));
    GraphicsPath clip;
    RoundPath(clip, cover, (REAL)S(5));
    if (Bitmap* bitmap = GetCover(game.appId)) {
        Bitmap* baked = GetBaked(g_bakedRows, game.appId, bitmap, (int)cover.Width, (int)cover.Height, (REAL)S(5), false, !game.installed);
        g.DrawImage(baked, Rect((int)cover.X, (int)cover.Y, (int)cover.Width, (int)cover.Height));
    } else {
        SolidBrush brush(kCover);
        g.FillPath(&brush, &clip);
        DrawCentered(g, game.name.substr(0, 1), (REAL)S(18), true, kAccentHover, cover);
    }

    if (hover && !g_drag.active && game.installed) {
        SolidBrush dim(Color(110, 0, 0, 0));
        g.FillPath(&dim, &clip);
        REAL diameter = (REAL)S(30), cx = cover.X + cover.Width / 2, cy = cover.Y + cover.Height / 2;
        bool hot = g_hover.cover;
        SolidBrush disc(hot ? kAccent : Color(230, 42, 42, 42));
        g.FillEllipse(&disc, cx - diameter / 2, cy - diameter / 2, diameter, diameter);
        DrawPlayTriangle(g, cx, cy, (REAL)S(12), hot ? kOnAccent : kText);
    }

    REAL tx = cover.GetRight() + S(16), tw = slot.GetRight() - tx - S(16);
    if (game.achTotal > 0) {  // achievement progress on the right of the row
        REAL barW = (REAL)S(150), barX = slot.GetRight() - S(16) - barW;
        DrawLabel(g, std::to_wstring(game.achDone) + L" / " + std::to_wstring(game.achTotal) + L" Erfolge", (REAL)S(12), false,
            kText2, RectF(barX, slot.Y + S(12), barW, (REAL)S(18)), StringAlignmentFar);
        DrawProgress(g, RectF(barX, slot.Y + S(36), barW, (REAL)S(6)), game.achDone, game.achTotal);
        tw -= barW + S(24);
    }
    DrawLabel(g, game.name, (REAL)S(15), true, selected ? kAccentHover : (game.installed ? kText : kText2),
        RectF(tx, slot.Y + S(10), tw, (REAL)S(22)));
    DrawLabel(g, PlayInfo(game), (REAL)S(13), false, kText2, RectF(tx, slot.Y + S(32), tw, (REAL)S(18)));
}

// A card in the grid, modeled on Spotify's: art with a round play button, then title and details.
void DrawCard(Graphics& g, int visibleIndex) {
    const Game& game = g_games[g_visible[visibleIndex]];
    bool selected = g_visible[visibleIndex] == g_sel;
    bool hover = visibleIndex == g_hover.row && !g_drag.active;

    RectF cell = RF(ItemCell(visibleIndex));
    if (selected) FillRound(g, cell, (REAL)S(8), kSurface);
    else if (hover) FillRound(g, cell, (REAL)S(8), kSurfaceHover);

    RectF art = RF(ArtRect(visibleIndex));
    GraphicsPath clip;
    RoundPath(clip, art, (REAL)S(6));
    Bitmap* portrait = GetPortrait(game.appId);
    Bitmap* source = portrait ? portrait : GetCover(game.appId);  // without portrait art, use the middle of the header
    if (source) {
        Bitmap* baked = GetBaked(g_bakedCards, game.appId, source, g_L.artW, g_L.artH, (REAL)S(6), portrait == nullptr, !game.installed);
        g.DrawImage(baked, Rect((int)art.X, (int)art.Y, g_L.artW, g_L.artH));
    } else {
        SolidBrush brush(kCover);
        g.FillPath(&brush, &clip);
        DrawCentered(g, game.name.substr(0, 1), (REAL)S(40), true, kAccentHover, art);
    }

    if ((hover || selected) && game.installed) {
        RectF play = RF(PlayRect(visibleIndex));
        bool hot = hover && g_hover.cover;
        REAL grow = hot ? (REAL)S(3) : 0.0f;
        RectF disc(play.X - grow, play.Y - grow, play.Width + 2 * grow, play.Height + 2 * grow);
        SolidBrush shadow(Color(80, 0, 0, 0));
        g.FillEllipse(&shadow, disc.X, disc.Y + 2, disc.Width, disc.Height);
        SolidBrush brush(hot ? kAccentHover : kAccent);
        g.FillEllipse(&brush, disc);
        DrawPlayTriangle(g, disc.X + disc.Width / 2, disc.Y + disc.Height / 2, (REAL)S(16), kOnAccent);
    }

    REAL textTop = art.GetBottom() + S(10);
    DrawLabel(g, game.name, (REAL)S(15), true, selected ? kAccentHover : (game.installed ? kText : kText2),
        RectF(art.X, textTop, art.Width, (REAL)S(20)));
    // Play time (with the size next to it) and last played on their own lines.
    PlayParts stats = PlayStats(game);
    std::wstring first = stats.playtime, second;
    if (!game.installed) {
        first = L"Nicht installiert";
        if (stats.played) second = stats.playtime;
    } else if (stats.played) {
        if (!stats.size.empty()) first += L"  ·  " + stats.size;
        second = L"Zuletzt " + stats.when;
    } else {
        second = stats.size;  // "Noch nicht gespielt" is long, so the size gets the next line
    }
    DrawLabel(g, first, (REAL)S(13), false, kText2, RectF(art.X, textTop + S(22), art.Width, (REAL)S(17)));
    if (!second.empty())
        DrawLabel(g, second, (REAL)S(13), false, kText2, RectF(art.X, textTop + S(39), art.Width, (REAL)S(17)));
    if (game.achTotal > 0) {  // achievement progress as a bar with the count next to it
        REAL labelW = (REAL)S(44), barY = textTop + S(62);
        DrawProgress(g, RectF(art.X, barY, art.Width - labelW - S(8), (REAL)S(6)), game.achDone, game.achTotal);
        DrawLabel(g, std::to_wstring(game.achDone) + L"/" + std::to_wstring(game.achTotal), (REAL)S(12), false, kText2,
            RectF(art.GetRight() - labelW, barY - S(6), labelW, (REAL)S(18)), StringAlignmentFar, StringAlignmentCenter);
    }
}

}  // namespace

void DrawList(Graphics& g, int W) {
    RectF listRect = RF(g_L.list);
    GraphicsState clipState = g.Save();
    g.SetClip(listRect, CombineModeIntersect);
    if (g_visible.empty()) {
        bool searching = !g_search.empty();
        DrawLabel(g, searching ? L"Keine Treffer für „" + g_search + L"“" : L"Hier ist noch nichts", (REAL)S(18), true, kText,
            RectF(listRect.X, listRect.Y + S(40), listRect.Width, (REAL)S(26)), StringAlignmentCenter);
        DrawLabel(g, searching ? L"Prüfe die Schreibweise oder lösche die Suche."
                               : L"Wähle eine andere Kategorie oder scanne Steam neu.", (REAL)S(13), false, kText2,
            RectF(listRect.X, listRect.Y + S(70), listRect.Width, (REAL)S(20)), StringAlignmentCenter);
    } else {
        for (int i = (g_scroll / g_L.rowH) * g_L.cols; i < (int)g_visible.size() && ItemTop(i) < g_L.list.bottom; ++i) {
            if (g_L.grid) DrawCard(g, i);
            else DrawRow(g, i, W);
        }

        int maxScroll = MaxScroll();
        if (maxScroll > 0) {
            REAL view = listRect.Height, content = (REAL)ContentHeight();
            REAL thumbH = std::max((REAL)S(32), view * view / content);
            REAL thumbY = listRect.Y + (view - thumbH) * g_scroll / maxScroll;
            FillRound(g, RectF((REAL)(g_L.list.right - S(8)), thumbY, (REAL)S(4), thumbH), (REAL)S(2), Color(90, 255, 255, 255));
        }
    }
    g.Restore(clipState);
}

}  // namespace playr
