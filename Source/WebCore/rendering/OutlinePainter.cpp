/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2005 Allan Sandfeld Jensen (kde@carewolf.com)
 *           (C) 2005, 2006 Samuel Weinig (sam.weinig@gmail.com)
 * Copyright (C) 2005-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"
#include "OutlinePainter.h"

#include "BorderEdge.h"
#include "BorderPainter.h"
#include "BorderShape.h"
#include "FloatRoundedRect.h"
#include "GeometryUtilities.h"
#include "GraphicsContext.h"
#include "PaintInfo.h"
#include "PathUtilities.h"
#include "RenderBox.h"
#include "RenderObjectDocument.h"
#include "RenderStyleInlines.h"
#include "RenderTheme.h"
#include "StylePrimitiveNumericTypes+Evaluation.h"

namespace WebCore {

OutlinePainter::OutlinePainter(const RenderElement& renderer, const PaintInfo& paintInfo)
    : m_renderer(renderer)
    , m_paintInfo(paintInfo)
{
}

void OutlinePainter::paintOutline(const LayoutRect& paintRect) const
{
    auto renderer = CheckedRef { m_renderer };
    auto styleToUse = CheckedRef { renderer->style() };

    // Only paint the focus ring by hand if the theme isn't able to draw it.
    if (styleToUse->outlineStyle() == OutlineStyle::Auto && !renderer->theme().supportsFocusRing(renderer, styleToUse.get())) {
        Vector<LayoutRect> focusRingRects;
        LayoutRect paintRectToUse { paintRect };
        if (CheckedPtr box = dynamicDowncast<RenderBox>(renderer.get()))
            paintRectToUse = renderer->theme().adjustedPaintRect(*box, paintRectToUse);
        renderer->addFocusRingRects(focusRingRects, paintRectToUse.location(), m_paintInfo.paintContainer);
        renderer->paintFocusRing(m_paintInfo, styleToUse.get(), focusRingRects);
    }

    if (renderer->hasOutlineAnnotation() && styleToUse->outlineStyle() != OutlineStyle::Auto && !renderer->theme().supportsFocusRing(renderer, styleToUse.get()))
        renderer->addPDFURLRect(m_paintInfo, paintRect.location());

    auto borderStyle = toBorderStyle(styleToUse->outlineStyle());
    if (!borderStyle || *borderStyle == BorderStyle::None)
        return;

    auto outlineWidth = Style::evaluate<LayoutUnit>(styleToUse->outlineWidth(), styleToUse->usedZoomForLength());
    auto outlineOffset = Style::evaluate<LayoutUnit>(styleToUse->outlineOffset(), Style::ZoomNeeded { });

    auto outerRect = paintRect;
    outerRect.inflate(outlineOffset + outlineWidth);
    // FIXME: This prevents outlines from painting inside the object http://webkit.org/b/12042.
    if (outerRect.isEmpty())
        return;

    auto hasBorderRadius = styleToUse->hasBorderRadius();
    auto closedEdges = RectEdges<bool> { true };

    auto outlineEdgeWidths = RectEdges<LayoutUnit> { outlineWidth };
    auto outlineShape = BorderShape::shapeForOutsetRect(styleToUse.get(), paintRect, outerRect, outlineEdgeWidths, closedEdges);

    auto bleedAvoidance = BleedAvoidance::ShrinkBackground;
    auto appliedClipAlready = false;
    auto edges = borderEdgesForOutline(styleToUse, *borderStyle, renderer->protectedDocument()->deviceScaleFactor());
    auto haveAllSolidEdges = BorderPainter::decorationHasAllSolidEdges(edges);

    BorderPainter { renderer, m_paintInfo }.paintSides(outlineShape, {
        hasBorderRadius ? std::make_optional(styleToUse->borderRadii()) : std::nullopt,
        edges,
        haveAllSolidEdges,
        outlineShape.outerShapeIsRectangular(),
        outlineShape.innerShapeIsRectangular(),
        bleedAvoidance,
        closedEdges,
        appliedClipAlready,
    });
}

void OutlinePainter::paintOutline(const LayoutPoint& paintOffset, const Vector<LayoutRect>& lineRects) const
{
    if (lineRects.size() == 1) {
        auto adjustedPaintRect = lineRects[0];
        adjustedPaintRect.moveBy(paintOffset);
        paintOutline(adjustedPaintRect);
        return;
    }

    auto renderer = CheckedRef { m_renderer };
    auto styleToUse = CheckedRef { renderer->style() };

    auto outlineOffset = Style::evaluate<float>(styleToUse->outlineOffset(), Style::ZoomNeeded { });
    auto outlineWidth = Style::evaluate<float>(styleToUse->outlineWidth(), styleToUse->usedZoomForLength());
    auto deviceScaleFactor = renderer->protectedDocument()->deviceScaleFactor();

    Vector<FloatRect> pixelSnappedRects;
    for (size_t index = 0; index < lineRects.size(); ++index) {
        auto rect = lineRects[index];

        rect.moveBy(paintOffset);
        rect.inflate(outlineOffset + outlineWidth / 2);
        pixelSnappedRects.append(snapRectToDevicePixels(rect, deviceScaleFactor));
    }
    auto path = PathUtilities::pathWithShrinkWrappedRectsForOutline(pixelSnappedRects, styleToUse->border().radii(), outlineOffset, styleToUse->writingMode(), deviceScaleFactor);
    if (path.isEmpty()) {
        // Disjoint line spanning inline boxes.
        for (auto rect : lineRects) {
            rect.moveBy(paintOffset);
            paintOutline(rect);
        }
        return;
    }

    auto& graphicsContext = m_paintInfo.context();
    auto outlineColor = styleToUse->visitedDependentColorWithColorFilter(CSSPropertyOutlineColor);
    auto useTransparencyLayer = !outlineColor.isOpaque();
    if (useTransparencyLayer) {
        graphicsContext.beginTransparencyLayer(outlineColor.alphaAsFloat());
        outlineColor = outlineColor.opaqueColor();
    }

    graphicsContext.setStrokeColor(outlineColor);
    graphicsContext.setStrokeThickness(outlineWidth);
    graphicsContext.setStrokeStyle(StrokeStyle::SolidStroke);
    graphicsContext.strokePath(path);

    if (useTransparencyLayer)
        graphicsContext.endTransparencyLayer();
}

}
