#include "ui/Draw.h"

#include "ui/DrawPrimitives.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

namespace {

RECT g_dirty{};  // the part of the window that is being repainted

// Parts of the window outside the repainted area are skipped: GDI+ clips them, but it still lays out their text and
// builds their paths first, which adds up when only the list or the reel changed.
bool Needs(const RECT& area) {
    RECT overlap;
    return IntersectRect(&overlap, &area, &g_dirty) != 0;
}

void DrawUi(Graphics& g, int W, int H) {
    SolidBrush window(kBar);
    g.FillRectangle(&window, 0, 0, W, H);
    bool library = g_page == Page::Library;
    FillRound(g, RF(library ? g_L.main : g_L.panel), (REAL)S(8), kBg);

    if (Needs(g_L.topBar)) {
        DrawTopBar(g);
        DrawTabs(g);
        DrawCaptionButtons(g);
    }

    if (!library) {
        if (Needs(g_L.topBar)) DrawActionButtons(g);
        RECT filters{ g_L.panel.left, g_L.panel.top, g_L.panel.right, g_L.stage.top };
        if (Needs(filters)) DrawFilters(g);
        if (Needs(g_L.stage)) DrawStage(g);
        DrawToast(g);
        return;
    }

    if (Needs(g_L.topBar)) DrawSearchField(g);
    RECT header{ g_L.main.left, g_L.main.top, g_L.main.right, g_L.list.top };
    if (Needs(header)) {
        // The main panel is titled with the category that is open.
        RectF title((REAL)(g_L.main.left + S(kMargin)), (REAL)g_L.main.top, (REAL)(g_L.main.right - g_L.main.left - 2 * S(kMargin)), 0);
        DrawLabel(g, SelectedCategory(), (REAL)S(32), true, kText,
            RectF(title.X, title.Y + S(18), (REAL)(g_L.toggle.left - S(24) - (int)title.X), (REAL)S(46)));
        DrawSortButton(g);
        DrawUninstalledToggle(g);
    }
    if (Needs(g_L.sidebar)) DrawSidebar(g);
    if (Needs(g_L.list)) DrawList(g, W);
    if (Needs(g_L.bar)) DrawBar(g, W, H);
    DrawToast(g);
    DrawMenu(g);
    DrawDragGhost(g);
}

}  // namespace

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HRGN updateRegion = CreateRectRgn(0, 0, 0, 0);
    int updateKind = GetUpdateRgn(hwnd, updateRegion, FALSE);
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    if (W > 0 && H > 0) {
        // The back buffer lives as long as the window size does, and only the invalid part is drawn and copied.
        static HDC mem = nullptr;
        static HBITMAP bmp = nullptr;
        static int bufferW = 0, bufferH = 0;
        if (!mem) mem = CreateCompatibleDC(hdc);
        if (bufferW != W || bufferH != H) {
            HBITMAP created = CreateCompatibleBitmap(hdc, W, H);
            SelectObject(mem, created);
            if (bmp) DeleteObject(bmp);
            bmp = created;
            bufferW = W;
            bufferH = H;
        }
        {
            Graphics g(mem);
            const RECT& dirty = ps.rcPaint;
            g_dirty = dirty;
            // The real update region, not just its bounding box: two small areas far apart stay two small areas.
            if (updateKind == COMPLEXREGION) { Region region(updateRegion); g.SetClip(&region); }
            else g.SetClip(Rect(dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            g.SetInterpolationMode(InterpolationModeBilinear);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            DrawUi(g, W, H);
        }
        BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
            ps.rcPaint.bottom - ps.rcPaint.top, mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
    DeleteObject(updateRegion);
}

}  // namespace playr
