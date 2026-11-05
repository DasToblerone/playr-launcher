#pragma once

#include <string>

#include "platform/Gdi.h"
#include "ui/Theme.h"

namespace playr {

Gdiplus::Color WithAlpha(const Gdiplus::Color& c, Gdiplus::REAL alpha);
void RoundPath(Gdiplus::GraphicsPath& path, Gdiplus::RectF r, Gdiplus::REAL radius);
void FillRound(Gdiplus::Graphics& g, Gdiplus::RectF r, Gdiplus::REAL radius, const Gdiplus::Color& color);

// Strokes a rounded outline inside `r` so the line stays within the rectangle.
void StrokeRound(Gdiplus::Graphics& g, Gdiplus::RectF r, Gdiplus::REAL radius, const Gdiplus::Color& color,
    Gdiplus::REAL width, bool dashed = false);
void StrokePill(Gdiplus::Graphics& g, Gdiplus::RectF r, const Gdiplus::Color& color, Gdiplus::REAL width,
    bool dashed = false);

const Gdiplus::Font& CachedFont(Gdiplus::REAL px, bool bold);

// `wrap` lets the text use as many lines as the rectangle holds and ends the last one with an ellipsis.
void DrawLabel(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::REAL px, bool bold, const Gdiplus::Color& color,
    Gdiplus::RectF r, Gdiplus::StringAlignment h = Gdiplus::StringAlignmentNear,
    Gdiplus::StringAlignment v = Gdiplus::StringAlignmentNear, bool wrap = false);
void DrawCentered(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::REAL px, bool bold,
    const Gdiplus::Color& color, Gdiplus::RectF r);
void DrawPlayTriangle(Gdiplus::Graphics& g, Gdiplus::REAL cx, Gdiplus::REAL cy, Gdiplus::REAL size,
    const Gdiplus::Color& color);
void DrawIcon(Gdiplus::Graphics& g, Icon icon, Gdiplus::REAL cx, Gdiplus::REAL cy, Gdiplus::REAL size,
    const Gdiplus::Color& color);

// Icon and label as one group, centered in a button.
void DrawButtonContent(Gdiplus::Graphics& g, Gdiplus::RectF r, Icon icon, const std::wstring& text,
    const Gdiplus::Color& color, Gdiplus::REAL px = 13);

}  // namespace playr
