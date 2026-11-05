#include "ui/Draw.h"

#include <algorithm>

#include "app/Messages.h"
#include "model/Library.h"
#include "steam/Steam.h"
#include "ui/ArtCache.h"
#include "ui/DrawPrimitives.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

// Steam keeps the profile picture of signed-in accounts in config/avatarcache.
void LoadAvatar() {
    if (g_steamId.empty()) return;
    std::error_code ec;
    fs::path path = fs::path(SteamPath()) / L"config" / L"avatarcache" / (g_steamId + L".png");
    if (!fs::exists(path, ec)) return;
    Bitmap* bitmap = Bitmap::FromFile(path.c_str());
    if (!bitmap || bitmap->GetLastStatus() != Ok) { delete bitmap; return; }
    g_avatar = bitmap;
}

// The top bar: the Steam account and how many games the library holds in total.
void DrawTopBar(Graphics& g) {
    RectF bar = RF(g_L.topBar);
    REAL avatar = (REAL)S(38), x = (REAL)S(kPanelGap + 8), cy = bar.Y + bar.Height / 2;
    RectF circle(x, cy - avatar / 2, avatar, avatar);
    std::wstring name = g_steamName.empty() ? L"Steam-Bibliothek" : g_steamName;
    if (g_avatar) {
        int size = (int)avatar;
        if (!g_avatarBaked || g_avatarSize != size) {  // round, scaled once to the size it is drawn at
            delete g_avatarBaked;
            g_avatarBaked = BakeArt(g_avatar, size, size, size / 2.0f, false);
            g_avatarSize = size;
        }
        g.DrawImage(g_avatarBaked, Rect((int)circle.X, (int)circle.Y, size, size));
    } else {  // no picture in the cache: the first letter of the name
        SolidBrush brush(kAccent);
        g.FillEllipse(&brush, circle);
        DrawCentered(g, name.substr(0, 1), (REAL)S(18), true, kOnAccent, circle);
    }

    REAL tx = circle.GetRight() + S(12), tw = (REAL)(S(kTabsLeft) - S(12)) - tx;  // the name ends before the tabs
    DrawLabel(g, name, (REAL)S(16), true, kText, RectF(tx, cy - S(20), tw, (REAL)S(22)));
    size_t installedCount = std::count_if(g_games.begin(), g_games.end(), [](const Game& game) { return game.installed; });
    DrawLabel(g, std::to_wstring(installedCount) + (installedCount == 1 ? L" Spiel installiert" : L" Spiele installiert"),
        (REAL)S(13), false, kText2, RectF(tx, cy + S(1), tw, (REAL)S(18)));
}

// The two tabs: the open one is a white pill, the other one a dark pill that lights up on hover.
void DrawTabs(Graphics& g) {
    for (int i = 0; i < 2; ++i) {
        RectF r = RF(g_L.tab[i]);
        bool active = (int)g_page == i, hover = g_hover.tab == i;
        Color fill = active ? kText : (hover ? kChipHover : kSurface);
        FillRound(g, r, r.Height / 2, fill);
        DrawCentered(g, kTabLabels[i], (REAL)S(14), true, active ? kOnAccent : kText, r);
    }
}

// Minimize, maximize (or restore) and close, drawn like Windows' own buttons.
void DrawCaptionButtons(Graphics& g) {
    if (g_fullscreen) return;
    bool zoomed = IsZoomed(g_mainWnd) != 0;
    for (int i = 0; i < 3; ++i) {
        RectF r = RF(g_L.caption[i]);
        bool hover = i == g_captionHover;
        if (hover) {
            SolidBrush brush(i == 2 ? Color(255, 232, 17, 35) : kSurface);
            g.FillRectangle(&brush, r);
        }
        Pen pen(kText, 1.0f);
        REAL cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, d = (REAL)S(5);
        if (i == 0) {
            g.DrawLine(&pen, cx - d, cy, cx + d, cy);
        } else if (i == 1) {
            if (zoomed) {  // two overlapping squares
                g.DrawRectangle(&pen, cx - d, cy - d + S(2), 2 * d - S(2), 2 * d - S(2));
                g.DrawLine(&pen, cx - d + S(2), cy - d + 1, cx + d, cy - d + 1);
                g.DrawLine(&pen, cx + d, cy - d + 1, cx + d, cy + d - S(2));
            } else {
                g.DrawRectangle(&pen, cx - d, cy - d, 2 * d, 2 * d);
            }
        } else {
            g.DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
            g.DrawLine(&pen, cx - d, cy + d, cx + d, cy - d);
        }
    }
}

// Search field: dark pill with a magnifying glass, a white ring while it has focus, and an "x" to clear it.
void DrawSearchField(Graphics& g) {
    RectF pill = RF(g_L.search);
    bool focus = GetFocus() == g_searchEdit;
    FillRound(g, pill, pill.Height / 2, kField);
    if (focus) StrokePill(g, pill, kText, 2.0f);
    DrawIcon(g, Icon::Search, pill.X + S(26), pill.Y + pill.Height / 2, (REAL)S(18), focus ? kText : kText2);
    if (!g_search.empty()) {
        RectF clear = RF(g_L.searchClear);
        REAL cx = clear.X + clear.Width / 2, cy = clear.Y + clear.Height / 2, d = (REAL)S(5);
        if (g_hover.searchClear) FillRound(g, clear, clear.Height / 2, kSurface);
        Pen pen(g_hover.searchClear ? kText : kText2, (REAL)S(1.8f));
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        g.DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
        g.DrawLine(&pen, cx - d, cy + d, cx + d, cy - d);
    }
}

}  // namespace playr
