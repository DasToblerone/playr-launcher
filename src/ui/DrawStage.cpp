#include "ui/Draw.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "model/Library.h"
#include "ui/ArtCache.h"
#include "ui/DrawPrimitives.h"
#include "ui/Format.h"
#include "ui/Layout.h"
#include "ui/Randomizer.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

namespace {

constexpr REAL kCenterScale = 1.08f, kEdgeScale = 0.78f;

REAL Smooth(REAL t) { return t * t * (3 - 2 * t); }

// The art of one card, scaled to the slot. Games that are not installed come out gray, like everywhere else.
// While the reel runs fast the scaling is the cheap kind: it is far too quick to see, and the full quality cost over
// three milliseconds per card.
void DrawCardArt(Graphics& g, const Game* game, RectF slot, bool fast) {
    REAL radius = (REAL)S(10);
    bool drawn = false;
    if (game) {
        Bitmap* portrait = GetPortrait(game->appId);
        Bitmap* source = portrait ? portrait : GetCover(game->appId);  // without portrait art, use the middle of the header
        if (source) {
            Bitmap* baked = GetBaked(g_bakedReel, game->appId, source, g_L.reelCardW, g_L.reelCardH, radius, portrait == nullptr,
                !game->installed);
            if (fast) {
                InterpolationMode interpolation = g.GetInterpolationMode();
                SmoothingMode smoothing = g.GetSmoothingMode();
                g.SetInterpolationMode(InterpolationModeNearestNeighbor);
                g.SetSmoothingMode(SmoothingModeNone);
                g.DrawImage(baked, slot);
                g.SetInterpolationMode(interpolation);
                g.SetSmoothingMode(smoothing);
            } else {
                g.DrawImage(baked, slot);
            }
            drawn = true;
        }
    }
    if (drawn) return;
    FillRound(g, slot, radius * slot.Width / g_L.reelCardW, kCover);
    if (game) DrawCentered(g, game->name.substr(0, 1), slot.Width * 0.3f, true, kAccentHover, slot);
}

// The soft edges of the reel: strips that run from the panel color to transparent, rendered once per size. A gradient
// brush over this much area costs several milliseconds per frame, a plain blit of a finished strip almost nothing.
Bitmap* FadeStrip(int w, int h, bool leftSide) {
    static Bitmap* strips[2] = { nullptr, nullptr };
    static int stripW = 0, stripH = 0;
    if (stripW != w || stripH != h) {
        delete strips[0];
        delete strips[1];
        strips[0] = strips[1] = nullptr;
        stripW = w;
        stripH = h;
    }
    Bitmap*& strip = strips[leftSide ? 0 : 1];
    if (strip || w <= 0 || h <= 0) return strip;
    strip = new Bitmap(w, h, PixelFormat32bppPARGB);
    BitmapData data;
    Rect all(0, 0, w, h);
    if (strip->LockBits(&all, ImageLockModeWrite, PixelFormat32bppPARGB, &data) != Ok) return strip;
    for (int x = 0; x < w; ++x) {
        REAL t = leftSide ? 1.0f - (x + 0.5f) / w : (x + 0.5f) / w;  // opaque at the panel edge
        BYTE a = (BYTE)std::lround(255 * t);
        uint32_t pixel = ((uint32_t)a << 24) | ((uint32_t)(kBg.GetR() * a / 255) << 16) |
            ((uint32_t)(kBg.GetG() * a / 255) << 8) | (uint32_t)(kBg.GetB() * a / 255);
        for (int y = 0; y < h; ++y) ((uint32_t*)((BYTE*)data.Scan0 + (size_t)y * data.Stride))[x] = pixel;
    }
    strip->UnlockBits(&data);
    return strip;
}

// A soft ring around the card in the middle; right after a landing it flashes outward once.
void DrawFrame(Graphics& g, bool idle) {
    RectF slot = RF(CenterCard());
    REAL pad = (REAL)S(7);
    slot = RectF(slot.X - pad, slot.Y - pad, slot.Width + 2 * pad, slot.Height + 2 * pad);
    REAL radius = (REAL)S(16);

    double since = (double)(GetTickCount64() - g_landedAt);
    if (idle && since < 900) {
        REAL t = (REAL)(since / 900.0), grow = (1.0f - (1.0f - t) * (1.0f - t)) * (REAL)S(30);
        RectF ring(slot.X - grow, slot.Y - grow, slot.Width + 2 * grow, slot.Height + 2 * grow);
        StrokeRound(g, ring, radius + grow, WithAlpha(kAccentHover, (1.0f - t) * 0.8f), (REAL)S(3));
    }
    if (idle) {  // glow
        for (int k = 3; k >= 1; --k) {
            REAL grow = (REAL)S(3.0f * k);
            RectF ring(slot.X - grow, slot.Y - grow, slot.Width + 2 * grow, slot.Height + 2 * grow);
            StrokeRound(g, ring, radius + grow, Color((BYTE)(34 / k), 192, 132, 252), (REAL)S(3));
        }
    }
    StrokeRound(g, slot, radius, idle ? kAccent : WithAlpha(kAccent, 0.55f), (REAL)S(2.5f));

    // Pointers above and below the slot, like the marker of a wheel.
    REAL cx = slot.X + slot.Width / 2, s = (REAL)S(9);
    SolidBrush brush(idle ? kAccent : WithAlpha(kAccent, 0.55f));
    PointF top[3] = { PointF(cx - s, slot.Y - s * 1.6f), PointF(cx + s, slot.Y - s * 1.6f), PointF(cx, slot.Y - s * 0.4f) };
    PointF bottom[3] = { PointF(cx - s, slot.GetBottom() + s * 1.6f), PointF(cx + s, slot.GetBottom() + s * 1.6f),
        PointF(cx, slot.GetBottom() + s * 0.4f) };
    g.FillPolygon(&brush, top, 3);
    g.FillPolygon(&brush, bottom, 3);
}

// The strip of covers. Cards shrink and darken toward the edges, and the edges fade into the panel.
void DrawReel(Graphics& g) {
    RectF area = RF(g_L.reel);
    GraphicsState state = g.Save();
    g.SetClip(area, CombineModeIntersect);

    double pos = ReelPosition();
    REAL pitch = (REAL)(g_L.reelCardW + S(18));
    REAL cx = (REAL)g_L.centerX, cy = (REAL)g_L.centerY;
    int span = (int)std::ceil(area.Width / 2 / pitch) + 2;
    int center = (int)std::lround(pos);
    bool fast = ReelSpeed() > 2.0;  // cards per second: too quick to see the difference, so scaling stays cheap
    int first = std::max(0, center - span), last = std::min((int)g_reel.size() - 1, center + span);

    // Far cards first, so the one in the middle ends up on top.
    for (int pass = span; pass >= 0; --pass) {
        for (int side = -1; side <= 1; side += 2) {
            int i = center + side * pass;
            if (i < first || i > last || (pass == 0 && side == 1)) continue;
            REAL offset = (REAL)(i - pos) * pitch;
            REAL closeness = std::min(1.0f, std::fabs(offset) / (pitch * 2.2f));
            REAL scale = kCenterScale - (kCenterScale - kEdgeScale) * Smooth(closeness);
            REAL w = g_L.reelCardW * scale, h = g_L.reelCardH * scale;
            RectF slot(cx + offset - w / 2, cy - h / 2, w, h);
            if (!g.IsVisible(slot)) continue;  // not in the region being repainted
            bool cheap = fast || (Spinning() && closeness > 0.45f);  // a slowing reel keeps full quality for the cards in the middle
            DrawCardArt(g, FindGame(g_reel[i]), slot, cheap);
            if (closeness > 0.02f) FillRound(g, slot, (REAL)S(10) * scale, Color((BYTE)(170 * Smooth(closeness)), 0, 0, 0));
        }
    }

    // Edges fade into the panel color.
    int fade = (int)std::min((REAL)S(260), area.Width * 0.3f), stripH = (int)area.Height;
    if (Bitmap* strip = FadeStrip(fade, stripH, true)) g.DrawImage(strip, Rect((int)area.X, (int)area.Y, fade, stripH));
    if (Bitmap* strip = FadeStrip(fade, stripH, false)) g.DrawImage(strip, Rect((int)area.GetRight() - fade, (int)area.Y, fade, stripH));
    g.Restore(state);

    DrawFrame(g, !Spinning() && ResultGame());
}

void DrawPill(Graphics& g, RECT rect, bool hover, bool primary, Icon icon, const std::wstring& text) {
    RectF r = RF(rect);
    Color fill = primary ? (hover ? kAccentHover : kAccent) : (hover ? kChipHover : kSurface);
    FillRound(g, r, r.Height / 2, fill);
    DrawButtonContent(g, r, icon, text, primary ? kOnAccent : kText, 15);
}

// The name of the game that was drawn, the facts about it and the buttons.
void DrawInfo(Graphics& g) {
    const Game* result = ResultGame();
    RectF name = RF(g_L.name), stats = RF(g_L.stats);
    std::wstring title, line;
    Color titleColor = kText;
    REAL alpha = 1, slide = 0;

    if (Spinning()) {  // the title runs through the names, too, and settles with the cards
        const Game* under = FindGame(g_reel[std::clamp((int)std::lround(ReelPosition()), 0, (int)g_reel.size() - 1)]);
        title = under ? under->name : L"";
        titleColor = kText2;
    } else if (result) {
        double t = std::min(1.0, (double)(GetTickCount64() - g_landedAt) / 450.0);
        REAL eased = (REAL)(1.0 - std::pow(1.0 - t, 3.0));
        alpha = eased;
        slide = (1.0f - eased) * S(10);
        title = result->name;
        line = PlayInfo(*result);
        if (result->achTotal > 0)
            line += L"  ·  " + std::to_wstring(result->achDone) + L" / " + std::to_wstring(result->achTotal) + L" Erfolge";
    } else {
        title = L"Was spielst du heute?";
        size_t n = g_pool.size();
        line = std::to_wstring(n) + (n == 1 ? L" Spiel" : L" Spiele") + L" im Topf";
    }
    DrawLabel(g, title, (REAL)S(30), true, WithAlpha(titleColor, alpha), RectF(name.X, name.Y + slide, name.Width, name.Height),
        StringAlignmentCenter, StringAlignmentCenter);
    DrawLabel(g, line, (REAL)S(14), false, WithAlpha(kText2, alpha), RectF(stats.X, stats.Y + slide, stats.Width, stats.Height),
        StringAlignmentCenter, StringAlignmentCenter);

    if (Spinning()) {
        RectF r = RF(g_L.act[ACT_ROLL]);
        FillRound(g, r, r.Height / 2, kSurface);
        DrawCentered(g, L"Würfelt …", (REAL)S(15), true, kText2, r);
    } else if (result) {
        DrawPill(g, g_L.act[ACT_START], g_hover.action == ACT_START, true, Icon::Play, result->installed ? L"Starten" : L"Installieren");
        DrawPill(g, g_L.act[ACT_AGAIN], g_hover.action == ACT_AGAIN, false, Icon::Dice, L"Nochmal würfeln");
    } else {
        DrawPill(g, g_L.act[ACT_ROLL], g_hover.action == ACT_ROLL, true, Icon::Dice, L"Würfeln");
    }
}

void DrawChip(Graphics& g, const ChipHit& chip, bool active, bool hover) {
    RectF r = RF(chip.r);
    FillRound(g, r, r.Height / 2, active ? (hover ? kAccentHover : kAccent) : (hover ? kChipHover : kSurface));
    DrawLabel(g, chip.label, (REAL)S(13), true, active ? kOnAccent : kText,
        RectF(r.X + S(10), r.Y, r.Width - S(20), r.Height), StringAlignmentCenter, StringAlignmentCenter);
}

// Switch that lets the games that are not installed into the draw.
void DrawRollToggle(Graphics& g) {
    RectF r = RF(g_L.rollToggle);
    bool on = g_rollUninstalled, hover = g_hover.rollToggle;
    REAL trackW = (REAL)S(38), trackH = (REAL)S(22);
    RectF track(r.GetRight() - trackW, r.Y + (r.Height - trackH) / 2, trackW, trackH);
    FillRound(g, track, trackH / 2, on ? (hover ? kAccentHover : kAccent) : (hover ? Color(255, 110, 110, 110) : Color(255, 84, 84, 84)));
    REAL knob = trackH - S(6);
    SolidBrush brush(Color(255, 255, 255, 255));
    g.FillEllipse(&brush, on ? track.GetRight() - knob - S(3) : track.X + S(3), track.Y + (trackH - knob) / 2, knob, knob);
    DrawLabel(g, L"Nicht installierte", (REAL)S(13), true, hover ? kText : kText2,
        RectF(r.X, r.Y, r.Width - trackW - S(10), r.Height), StringAlignmentFar, StringAlignmentCenter);
}

}  // namespace

void DrawFilters(Graphics& g) {
    const wchar_t* captions[2] = { L"Spielzeit", L"Sammlung" };
    for (int i = 0; i < 2; ++i)
        if (g_L.labelY[i] >= 0)
            DrawLabel(g, captions[i], (REAL)S(13), true, kText2,
                RectF((REAL)g_L.labelX, (REAL)g_L.labelY[i], (REAL)S(kLabelWidth), (REAL)g_L.labelH), StringAlignmentNear,
                StringAlignmentCenter);
    for (int i = 0; i < (int)g_L.rollChips.size(); ++i) {
        const ChipHit& chip = g_L.rollChips[i];
        bool active = chip.group == 0 ? (int)g_playFilter == chip.value : g_poolCategory == chip.value;
        DrawChip(g, chip, active, i == g_hover.rollChip);
    }
    DrawRollToggle(g);
}

void DrawStage(Graphics& g) {
    if (g_pool.empty()) {
        RectF area = RF(g_L.stage);
        bool noGames = g_games.empty();
        DrawLabel(g, noGames ? L"Keine Steam-Spiele gefunden" : L"Der Topf ist leer", (REAL)S(26), true, kText,
            RectF(area.X, area.Y + area.Height / 2 - S(50), area.Width, (REAL)S(40)), StringAlignmentCenter, StringAlignmentCenter);
        DrawLabel(g, noGames ? L"Starte Steam einmal oder scanne neu."
                             : L"Lockere die Filter oder nimm nicht installierte Spiele dazu.",
            (REAL)S(14), false, kText2, RectF(area.X, area.Y + area.Height / 2 - S(8), area.Width, (REAL)S(24)),
            StringAlignmentCenter, StringAlignmentCenter);
        return;
    }
    DrawReel(g);
    DrawInfo(g);
}

}  // namespace playr
