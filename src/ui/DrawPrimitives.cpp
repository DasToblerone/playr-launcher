#include "ui/DrawPrimitives.h"

#include <cmath>
#include <map>
#include <memory>

#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

Color WithAlpha(const Color& c, REAL alpha) {
    return Color((BYTE)std::lround(c.GetA() * std::clamp(alpha, 0.0f, 1.0f)), c.GetR(), c.GetG(), c.GetB());
}

void RoundPath(GraphicsPath& path, RectF r, REAL radius) {
    REAL d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

void FillRound(Graphics& g, RectF r, REAL radius, const Color& color) {
    GraphicsPath path;
    RoundPath(path, r, radius);
    SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

// Strokes a rounded outline inside `r` so the line stays within the rectangle.
void StrokeRound(Graphics& g, RectF r, REAL radius, const Color& color, REAL width, bool dashed) {
    GraphicsPath path;
    REAL inset = width / 2;
    RoundPath(path, RectF(r.X + inset, r.Y + inset, r.Width - 2 * inset, r.Height - 2 * inset), radius - inset);
    Pen pen(color, width);
    if (dashed) pen.SetDashStyle(DashStyleDash);
    g.DrawPath(&pen, &path);
}

void StrokePill(Graphics& g, RectF r, const Color& color, REAL width, bool dashed) {
    StrokeRound(g, r, r.Height / 2, color, width, dashed);
}

// Creating fonts is slow, and a repaint draws dozens of labels, so they are created once per size.
const Font& CachedFont(REAL px, bool bold) {
    static FontFamily family(L"Arial");
    static std::map<std::pair<int, bool>, std::unique_ptr<Font>> fonts;
    auto& font = fonts[{ (int)std::lround(px * 10), bold }];
    if (!font) font = std::make_unique<Font>(&family, px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    return *font;
}

// `wrap` lets the text use as many lines as the rectangle holds and ends the last one with an ellipsis.
void DrawLabel(Graphics& g, const std::wstring& text, REAL px, bool bold, const Color& color, RectF r,
    StringAlignment h, StringAlignment v, bool wrap) {
    const Font& font = CachedFont(px, bold);
    StringFormat format;
    format.SetAlignment(h);
    format.SetLineAlignment(v);
    format.SetTrimming(wrap ? StringTrimmingEllipsisWord : StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(wrap ? StringFormatFlagsLineLimit : StringFormatFlagsNoWrap);
    SolidBrush brush(color);
    g.DrawString(text.c_str(), -1, &font, r, &format, &brush);
}

void DrawCentered(Graphics& g, const std::wstring& text, REAL px, bool bold, const Color& color, RectF r) {
    DrawLabel(g, text, px, bold, color, r, StringAlignmentCenter, StringAlignmentCenter);
}

void DrawPlayTriangle(Graphics& g, REAL cx, REAL cy, REAL size, const Color& color) {
    PointF triangle[3] = { PointF(cx - size * 0.3f, cy - size / 2), PointF(cx - size * 0.3f, cy + size / 2),
        PointF(cx + size * 0.6f, cy) };
    SolidBrush brush(color);
    g.FillPolygon(&brush, triangle, 3);
}

void DrawIcon(Graphics& g, Icon icon, REAL cx, REAL cy, REAL size, const Color& color) {
    Pen pen(color, (REAL)S(1.8f));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    REAL h = size / 2;
    switch (icon) {
    case Icon::Refresh: {
        // An open circle with an arrow head where it ends.
        REAL r = h * 0.78f, theta = 4.18879f;  // 240 degrees
        g.DrawArc(&pen, cx - r, cy - r, 2 * r, 2 * r, -60, 300);
        PointF end(cx + r * std::cos(theta), cy + r * std::sin(theta));
        PointF tangent(-std::sin(theta), std::cos(theta)), normal(-tangent.Y, tangent.X);
        REAL s = size * 0.3f;
        PointF head[3] = { PointF(end.X + tangent.X * s * 0.5f, end.Y + tangent.Y * s * 0.5f),
            PointF(end.X - tangent.X * s * 0.5f + normal.X * s * 0.8f, end.Y - tangent.Y * s * 0.5f + normal.Y * s * 0.8f),
            PointF(end.X - tangent.X * s * 0.5f - normal.X * s * 0.8f, end.Y - tangent.Y * s * 0.5f - normal.Y * s * 0.8f) };
        SolidBrush brush(color);
        g.FillPolygon(&brush, head, 3);
        break;
    }
    case Icon::Plus:
        g.DrawLine(&pen, cx - h * 0.7f, cy, cx + h * 0.7f, cy);
        g.DrawLine(&pen, cx, cy - h * 0.7f, cx, cy + h * 0.7f);
        break;
    case Icon::Search: {  // a magnifying glass
        REAL r = h * 0.6f;
        g.DrawEllipse(&pen, cx - h * 0.2f - r, cy - h * 0.2f - r, 2 * r, 2 * r);
        g.DrawLine(&pen, cx - h * 0.2f + r * 0.72f, cy - h * 0.2f + r * 0.72f, cx + h * 0.85f, cy + h * 0.85f);
        break;
    }
    case Icon::Sort:  // three bars that get shorter
        g.DrawLine(&pen, cx - h * 0.85f, cy - h * 0.6f, cx + h * 0.85f, cy - h * 0.6f);
        g.DrawLine(&pen, cx - h * 0.85f, cy, cx + h * 0.4f, cy);
        g.DrawLine(&pen, cx - h * 0.85f, cy + h * 0.6f, cx - h * 0.05f, cy + h * 0.6f);
        break;
    case Icon::Library:  // three bars, the last one leaning, like Spotify's library icon
        g.DrawLine(&pen, cx - h * 0.7f, cy - h * 0.75f, cx - h * 0.7f, cy + h * 0.75f);
        g.DrawLine(&pen, cx - h * 0.1f, cy - h * 0.75f, cx - h * 0.1f, cy + h * 0.75f);
        g.DrawLine(&pen, cx + h * 0.45f, cy - h * 0.7f, cx + h * 0.85f, cy + h * 0.75f);
        break;
    case Icon::Folder: {
        PointF outline[6] = { PointF(cx - h * 0.85f, cy + h * 0.6f), PointF(cx - h * 0.85f, cy - h * 0.6f),
            PointF(cx - h * 0.2f, cy - h * 0.6f), PointF(cx + h * 0.05f, cy - h * 0.3f),
            PointF(cx + h * 0.85f, cy - h * 0.3f), PointF(cx + h * 0.85f, cy + h * 0.6f) };
        g.DrawPolygon(&pen, outline, 6);
        break;
    }
    case Icon::Download: {
        g.DrawLine(&pen, cx, cy - h * 0.85f, cx, cy + h * 0.25f);
        PointF arrow[3] = { PointF(cx - h * 0.5f, cy - h * 0.25f), PointF(cx, cy + h * 0.3f), PointF(cx + h * 0.5f, cy - h * 0.25f) };
        g.DrawLines(&pen, arrow, 3);
        PointF tray[4] = { PointF(cx - h * 0.8f, cy + h * 0.35f), PointF(cx - h * 0.8f, cy + h * 0.85f),
            PointF(cx + h * 0.8f, cy + h * 0.85f), PointF(cx + h * 0.8f, cy + h * 0.35f) };
        g.DrawLines(&pen, tray, 4);
        break;
    }
    case Icon::Dice: {  // a die showing five
        REAL half = h * 0.9f;
        GraphicsPath outline;
        RoundPath(outline, RectF(cx - half, cy - half, 2 * half, 2 * half), half * 0.38f);
        g.DrawPath(&pen, &outline);
        SolidBrush brush(color);
        REAL pip = size * 0.075f, step = half * 0.5f;
        for (REAL dx : { -step, 0.0f, step })
            for (REAL dy : { -step, 0.0f, step })
                if ((dx == 0) == (dy == 0)) g.FillEllipse(&brush, cx + dx - pip, cy + dy - pip, 2 * pip, 2 * pip);
        break;
    }
    case Icon::Play:
        DrawPlayTriangle(g, cx, cy, size * 0.8f, color);
        break;
    case Icon::None:
        break;
    }
}

// Icon and label as one group, centered in a button.
void DrawButtonContent(Graphics& g, RectF r, Icon icon, const std::wstring& text, const Color& color, REAL px) {
    REAL scale = px / 13.0f;
    RectF box;
    g.MeasureString(text.c_str(), -1, &CachedFont((REAL)S(px), true), PointF(0, 0), StringFormat::GenericTypographic(), &box);
    REAL iconW = icon == Icon::None ? 0.0f : (REAL)S(16 * scale), gap = icon == Icon::None ? 0.0f : (REAL)S(8 * scale);
    REAL textW = std::ceil(box.Width);
    REAL startX = r.X + (r.Width - (iconW + gap + textW)) / 2;
    DrawIcon(g, icon, startX + iconW / 2, r.Y + r.Height / 2, iconW, color);
    DrawLabel(g, text, (REAL)S(px), true, color,
        RectF(startX + iconW + gap, r.Y, textW + S(12), r.Height), StringAlignmentNear, StringAlignmentCenter);
}

}  // namespace playr
