#include "ui/Draw.h"

#include "ui/DrawPrimitives.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

namespace {

void DrawUi(Graphics& g, int W, int H) {
    SolidBrush window(kBar);
    g.FillRectangle(&window, 0, 0, W, H);
    FillRound(g, RF(g_L.main), (REAL)S(8), kBg);

    DrawTopBar(g);
    DrawSearchField(g);
    DrawCaptionButtons(g);

    // The main panel is titled with the category that is open.
    RectF header((REAL)(g_L.main.left + S(kMargin)), (REAL)g_L.main.top, (REAL)(g_L.main.right - g_L.main.left - 2 * S(kMargin)), 0);
    DrawLabel(g, SelectedCategory(), (REAL)S(32), true, kText,
        RectF(header.X, header.Y + S(18), (REAL)(g_L.toggle.left - S(24) - (int)header.X), (REAL)S(46)));

    DrawSidebar(g);
    DrawSortButton(g);
    DrawUninstalledToggle(g);
    DrawList(g, W);
    DrawBar(g, W, H);
    DrawToast(g);
    DrawMenu(g);
    DrawDragGhost(g);
}

}  // namespace

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
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
            g.SetClip(Rect(dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top));
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
}

}  // namespace playr
