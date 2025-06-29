/*
 * Copyright (C) 2004, 2005 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006 Rob Buis <buis@kde.org>
 * Copyright (C) 2009 Google, Inc. All rights reserved.
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
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

#include "config.h"
#include "SVGLocatable.h"

#include "ContainerNodeInlines.h"
#include "RenderElement.h"
#include "SVGElementTypeHelpers.h"
#include "SVGGraphicsElement.h"
#include "SVGImageElement.h"
#include "SVGLayerTransformComputation.h"
#include "SVGMatrix.h"
#include "SVGNames.h"
#include "TransformState.h"

namespace WebCore {

// FIXME: This doesn't match SVGElement::viewportElement() as it has an extra check for
// foreign object.
static bool isViewportElement(const SVGElement* element)
{
    if (!element)
        return false;

    return element->hasTagName(SVGNames::svgTag)
        || element->hasTagName(SVGNames::symbolTag)
        || element->hasTagName(SVGNames::foreignObjectTag)
        || is<SVGImageElement>(*element);
}

SVGElement* SVGLocatable::nearestViewportElement(const SVGElement* element)
{
    ASSERT(element);
    for (RefPtr current = element->parentOrShadowHostElement(); current; current = current->parentOrShadowHostElement()) {
        auto* svgElement = dynamicDowncast<SVGElement>(*current);
        if (isViewportElement(svgElement))
            return svgElement;
    }

    return nullptr;
}

SVGElement* SVGLocatable::farthestViewportElement(const SVGElement* element)
{
    ASSERT(element);
    SUPPRESS_UNCOUNTED_LOCAL SVGElement* farthest = nullptr;
    for (RefPtr current = element->parentOrShadowHostElement(); current; current = current->parentOrShadowHostElement()) {
        auto* svgElement = dynamicDowncast<SVGElement>(*current);
        if (isViewportElement(svgElement))
            farthest = svgElement;
    }
    return farthest;
}

FloatRect SVGLocatable::getBBox(SVGElement* element, StyleUpdateStrategy styleUpdateStrategy)
{
    ASSERT(element);
    if (styleUpdateStrategy == AllowStyleUpdate)
        element->protectedDocument()->updateLayoutIgnorePendingStylesheets({ LayoutOptions::TreatContentVisibilityHiddenAsVisible, LayoutOptions::TreatContentVisibilityAutoAsVisible }, element);

    // FIXME: Eventually we should support getBBox for detached elements.
    if (!element->renderer())
        return FloatRect();

    return element->renderer()->objectBoundingBox();
}

AffineTransform SVGLocatable::computeCTM(SVGElement* element, CTMScope mode, StyleUpdateStrategy styleUpdateStrategy)
{
    ASSERT(element);
    if (styleUpdateStrategy == AllowStyleUpdate)
        element->protectedDocument()->updateLayoutIgnorePendingStylesheets({ LayoutOptions::TreatContentVisibilityHiddenAsVisible, LayoutOptions::TreatContentVisibilityAutoAsVisible }, element);

    RefPtr stopAtElement = mode == CTMScope::NearestViewportScope ? nearestViewportElement(element) : nullptr;

    if (element->document().settings().layerBasedSVGEngineEnabled()) {
        // Rudimentary support for operations on "detached" elements.
        CheckedPtr renderer = dynamicDowncast<RenderLayerModelObject>(element->renderer());
        if (!renderer)
            return element->localCoordinateSpaceTransform(mode);

        auto trackingMode { mode == CTMScope::ScreenScope ? TransformState::TrackSVGScreenCTMMatrix : TransformState::TrackSVGCTMMatrix };
        CheckedPtr stopAtRenderer = dynamicDowncast<RenderLayerModelObject>(stopAtElement ? stopAtElement->renderer() : nullptr);
        return SVGLayerTransformComputation(*renderer).computeAccumulatedTransform(stopAtRenderer.get(), trackingMode);
    }

    AffineTransform ctm;

    for (Element* currentElement = element; currentElement; currentElement = currentElement->parentOrShadowHostElement()) {
        RefPtr svgElement = dynamicDowncast<SVGElement>(*currentElement);
        if (!svgElement)
            break;

        ctm = svgElement->localCoordinateSpaceTransform(mode).multiply(ctm);

        // For getCTM() computation, stop at the nearest viewport element
        if (currentElement == stopAtElement)
            break;
    }

    return ctm;
}

}
