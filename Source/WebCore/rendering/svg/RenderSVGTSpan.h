/*
 * Copyright (C) 2006 Oliver Hunt <ojh16@student.canterbury.ac.nz>
 * Copyright (C) 2006 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Google Inc.
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
 */

#pragma once

#include "RenderBoxModelObjectInlines.h"
#include "RenderSVGInline.h"
#include "SVGTextPositioningElement.h"

namespace WebCore {

class RenderSVGTSpan final : public RenderSVGInline {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(RenderSVGTSpan);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(RenderSVGTSpan);
public:
    explicit RenderSVGTSpan(SVGTextPositioningElement& element, RenderStyle&& style)
        : RenderSVGInline(Type::SVGTSpan, element, WTFMove(style))
    {
        ASSERT(isRenderSVGTSpan());
    }

private:
    void graphicsElement() const = delete;
    ASCIILiteral renderName() const override { return "RenderSVGTSpan"_s; }
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_RENDER_OBJECT(RenderSVGTSpan, isRenderSVGTSpan())
