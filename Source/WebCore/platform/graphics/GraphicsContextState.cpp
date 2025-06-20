/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "GraphicsContextState.h"

#include <wtf/MathExtras.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

GraphicsContextState::GraphicsContextState(const ChangeFlags& changeFlags, InterpolationQuality imageInterpolationQuality)
    : m_changeFlags(changeFlags)
    , m_imageInterpolationQuality(imageInterpolationQuality)
{
}

void GraphicsContextState::repurpose(Purpose purpose)
{
    if (purpose == Purpose::Initial)
        m_changeFlags = { };

#if USE(CG)
    // CGContextBeginTransparencyLayer() sets the CG global alpha to 1. Keep the clone's alpha in sync.
    if (purpose == Purpose::TransparencyLayer)
        m_alpha = 1;
#endif

    m_purpose = purpose;
}

GraphicsContextState GraphicsContextState::clone(Purpose purpose) const
{
    auto clone = *this;
    clone.repurpose(purpose);
    return clone;
}

void GraphicsContextState::mergeLastChanges(const GraphicsContextState& state, const std::optional<GraphicsContextState>& lastDrawingState)
{
    for (auto change : state.changes())
        mergeSingleChange(state, toIndex(change), lastDrawingState);
}

void GraphicsContextState::mergeSingleChange(const GraphicsContextState& state, ChangeIndex changeIndex, const std::optional<GraphicsContextState>& lastDrawingState)
{
    auto mergeChange = [&](auto GraphicsContextState::*property) {
        if (this->*property == state.*property)
            return;
        this->*property = state.*property;
        m_changeFlags.set(changeIndex.toChange(), !lastDrawingState || (*lastDrawingState).*property != this->*property);
    };

    switch (changeIndex.value) {
    case toIndex(Change::FillBrush).value:
        mergeChange(&GraphicsContextState::m_fillBrush);
        break;
    case toIndex(Change::FillRule).value:
        mergeChange(&GraphicsContextState::m_fillRule);
        break;

    case toIndex(Change::StrokeBrush).value:
        mergeChange(&GraphicsContextState::m_strokeBrush);
        break;
    case toIndex(Change::StrokeThickness).value:
        mergeChange(&GraphicsContextState::m_strokeThickness);
        break;
    case toIndex(Change::StrokeStyle).value:
        mergeChange(&GraphicsContextState::m_strokeStyle);
        break;

    case toIndex(Change::CompositeMode).value:
        mergeChange(&GraphicsContextState::m_compositeMode);
        break;
    case toIndex(Change::DropShadow).value:
        mergeChange(&GraphicsContextState::m_dropShadow);
        break;
    case toIndex(Change::Style).value:
        mergeChange(&GraphicsContextState::m_style);
        break;

    case toIndex(Change::Alpha).value:
        mergeChange(&GraphicsContextState::m_alpha);
        break;
    case toIndex(Change::TextDrawingMode).value:
        mergeChange(&GraphicsContextState::m_textDrawingMode);
        break;
    case toIndex(Change::ImageInterpolationQuality).value:
        mergeChange(&GraphicsContextState::m_imageInterpolationQuality);
        break;

    case toIndex(Change::ShouldAntialias).value:
        mergeChange(&GraphicsContextState::m_shouldAntialias);
        break;
    case toIndex(Change::ShouldSmoothFonts).value:
        mergeChange(&GraphicsContextState::m_shouldSmoothFonts);
        break;
    case toIndex(Change::ShouldSubpixelQuantizeFonts).value:
        mergeChange(&GraphicsContextState::m_shouldSubpixelQuantizeFonts);
        break;
    case toIndex(Change::ShadowsIgnoreTransforms).value:
        mergeChange(&GraphicsContextState::m_shadowsIgnoreTransforms);
        break;
    case toIndex(Change::DrawLuminanceMask).value:
        mergeChange(&GraphicsContextState::m_drawLuminanceMask);
        break;
    default:
        RELEASE_ASSERT_NOT_REACHED();
    }
}

void GraphicsContextState::mergeAllChanges(const GraphicsContextState& state)
{
    auto mergeChange = [&](Change change, auto GraphicsContextState::*property) {
        if (this->*property == state.*property)
            return;
        this->*property = state.*property;
        m_changeFlags.add(change);
    };

    mergeChange(Change::FillBrush,                   &GraphicsContextState::m_fillBrush);
    mergeChange(Change::FillRule,                    &GraphicsContextState::m_fillRule);

    mergeChange(Change::StrokeBrush,                 &GraphicsContextState::m_strokeBrush);
    mergeChange(Change::StrokeThickness,             &GraphicsContextState::m_strokeThickness);
    mergeChange(Change::StrokeStyle,                 &GraphicsContextState::m_strokeStyle);

    mergeChange(Change::CompositeMode,               &GraphicsContextState::m_compositeMode);
    mergeChange(Change::DropShadow,                  &GraphicsContextState::m_dropShadow);
    mergeChange(Change::Style,                       &GraphicsContextState::m_style);

    mergeChange(Change::Alpha,                       &GraphicsContextState::m_alpha);
    mergeChange(Change::ImageInterpolationQuality,   &GraphicsContextState::m_textDrawingMode);
    mergeChange(Change::TextDrawingMode,             &GraphicsContextState::m_imageInterpolationQuality);

    mergeChange(Change::ShouldAntialias,             &GraphicsContextState::m_shouldAntialias);
    mergeChange(Change::ShouldSmoothFonts,           &GraphicsContextState::m_shouldSmoothFonts);
    mergeChange(Change::ShouldSubpixelQuantizeFonts, &GraphicsContextState::m_shouldSubpixelQuantizeFonts);
    mergeChange(Change::ShadowsIgnoreTransforms,     &GraphicsContextState::m_shadowsIgnoreTransforms);
    mergeChange(Change::DrawLuminanceMask,           &GraphicsContextState::m_drawLuminanceMask);
}

static ASCIILiteral stateChangeName(GraphicsContextState::Change change)
{
    switch (change) {
    case GraphicsContextState::Change::FillBrush:
        return "fill-brush"_s;

    case GraphicsContextState::Change::FillRule:
        return "fill-rule"_s;

    case GraphicsContextState::Change::StrokeBrush:
        return "stroke-brush"_s;

    case GraphicsContextState::Change::StrokeThickness:
        return "stroke-thickness"_s;

    case GraphicsContextState::Change::StrokeStyle:
        return "stroke-style"_s;

    case GraphicsContextState::Change::CompositeMode:
        return "composite-mode"_s;

    case GraphicsContextState::Change::DropShadow:
        return "drop-shadow"_s;

    case GraphicsContextState::Change::Style:
        return "style"_s;

    case GraphicsContextState::Change::Alpha:
        return "alpha"_s;

    case GraphicsContextState::Change::ImageInterpolationQuality:
        return "image-interpolation-quality"_s;

    case GraphicsContextState::Change::TextDrawingMode:
        return "text-drawing-mode"_s;

    case GraphicsContextState::Change::ShouldAntialias:
        return "should-antialias"_s;

    case GraphicsContextState::Change::ShouldSmoothFonts:
        return "should-smooth-fonts"_s;

    case GraphicsContextState::Change::ShouldSubpixelQuantizeFonts:
        return "should-subpixel-quantize-fonts"_s;

    case GraphicsContextState::Change::ShadowsIgnoreTransforms:
        return "shadows-ignore-transforms"_s;

    case GraphicsContextState::Change::DrawLuminanceMask:
        return "draw-luminance-mask"_s;
    }

    RELEASE_ASSERT_NOT_REACHED();
}

TextStream& GraphicsContextState::dump(TextStream& ts) const
{
    auto dump = [&](Change change, auto GraphicsContextState::*property) {
        if (m_changeFlags.contains(change))
            ts.dumpProperty(stateChangeName(change), this->*property);
    };

    ts.dumpProperty("change-flags"_s, m_changeFlags);

    dump(Change::FillBrush,                     &GraphicsContextState::m_fillBrush);
    dump(Change::FillRule,                      &GraphicsContextState::m_fillRule);

    dump(Change::StrokeBrush,                   &GraphicsContextState::m_strokeBrush);
    dump(Change::StrokeThickness,               &GraphicsContextState::m_strokeThickness);
    dump(Change::StrokeStyle,                   &GraphicsContextState::m_strokeStyle);

    dump(Change::CompositeMode,                 &GraphicsContextState::m_compositeMode);
    dump(Change::DropShadow,                    &GraphicsContextState::m_dropShadow);
    dump(Change::Style,                         &GraphicsContextState::m_style);

    dump(Change::Alpha,                         &GraphicsContextState::m_alpha);
    dump(Change::ImageInterpolationQuality,     &GraphicsContextState::m_imageInterpolationQuality);
    dump(Change::TextDrawingMode,               &GraphicsContextState::m_textDrawingMode);

    dump(Change::ShouldAntialias,               &GraphicsContextState::m_shouldAntialias);
    dump(Change::ShouldSmoothFonts,             &GraphicsContextState::m_shouldSmoothFonts);
    dump(Change::ShouldSubpixelQuantizeFonts,   &GraphicsContextState::m_shouldSubpixelQuantizeFonts);
    dump(Change::ShadowsIgnoreTransforms,       &GraphicsContextState::m_shadowsIgnoreTransforms);
    dump(Change::DrawLuminanceMask,             &GraphicsContextState::m_drawLuminanceMask);
    return ts;
}

TextStream& operator<<(TextStream& ts, GraphicsContextState::Change change)
{
    ts << stateChangeName(change);
    return ts;
}

TextStream& operator<<(TextStream& ts, const GraphicsContextState& state)
{
    return state.dump(ts);
}

} // namespace WebCore
