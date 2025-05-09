/*
 * Copyright (C) 2019 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ResizeObservation.h"

#include "ElementInlines.h"
#include "HTMLFrameOwnerElement.h"
#include "Logging.h"
#include "NodeInlines.h"
#include "RenderBoxInlines.h"
#include "RenderElementInlines.h"
#include "SVGElement.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

Ref<ResizeObservation> ResizeObservation::create(Element& target, ResizeObserverBoxOptions observedBox)
{
    return adoptRef(*new ResizeObservation(target, observedBox));
}

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(ResizeObservation);

ResizeObservation::ResizeObservation(Element& element, ResizeObserverBoxOptions observedBox)
    : m_target { element }
    , m_lastObservationSizes { LayoutSize(-1, -1), LayoutSize(-1, -1), LayoutSize(-1, -1) }
    , m_observedBox { observedBox }
{
}

ResizeObservation::~ResizeObservation() = default;

void ResizeObservation::updateObservationSize(const BoxSizes& boxSizes)
{
    m_lastObservationSizes = boxSizes;
}

void ResizeObservation::resetObservationSize()
{
    m_lastObservationSizes = { LayoutSize(-1, -1), LayoutSize(-1, -1), LayoutSize(-1, -1) };
}

auto ResizeObservation::computeObservedSizes() const -> std::optional<BoxSizes>
{
    if (auto* svg = dynamicDowncast<SVGElement>(target())) {
        if (svg->hasAssociatedSVGLayoutBox()) {
            LayoutSize size;
            if (auto svgRect = svg->getBoundingBox()) {
                size.setWidth(svgRect->width());
                size.setHeight(svgRect->height());
            }
            return { { size, size, size } };
        }
    }

    auto* box = m_target->renderBox();
    if (box) {
        if (box->isSkippedContent())
            return std::nullopt;
        return { {
            adjustLayoutSizeForAbsoluteZoom(box->contentBoxSize(), *box),
            adjustLayoutSizeForAbsoluteZoom(box->contentBoxLogicalSize(), *box),
            adjustLayoutSizeForAbsoluteZoom(box->borderBoxLogicalSize(), *box)
        } };
    }

    return BoxSizes { };
}

LayoutPoint ResizeObservation::computeTargetLocation() const
{
    if (!m_target->isSVGElement()) {
        if (auto box = m_target->renderBox())
            return LayoutPoint(box->paddingLeft(), box->paddingTop());
    }

    return { };
}

FloatRect ResizeObservation::computeContentRect() const
{
    return FloatRect(FloatPoint(computeTargetLocation()), FloatSize(m_lastObservationSizes.contentBoxSize));
}

FloatSize ResizeObservation::borderBoxSize() const
{
    return m_lastObservationSizes.borderBoxLogicalSize;
}

FloatSize ResizeObservation::contentBoxSize() const
{
    return m_lastObservationSizes.contentBoxLogicalSize;
}

FloatSize ResizeObservation::snappedContentBoxSize() const
{
    return m_lastObservationSizes.contentBoxLogicalSize; // FIXME: Need to pixel snap.
}

RefPtr<Element> ResizeObservation::protectedTarget() const
{
    return m_target.get();
}

std::optional<ResizeObservation::BoxSizes> ResizeObservation::elementSizeChanged() const
{
    auto currentSizes = computeObservedSizes();
    if (!currentSizes)
        return std::nullopt;

    switch (m_observedBox) {
    case ResizeObserverBoxOptions::BorderBox:
        if (m_lastObservationSizes.borderBoxLogicalSize != currentSizes->borderBoxLogicalSize) {
            LOG_WITH_STREAM(ResizeObserver, stream << "ResizeObservation " << *this << " elementSizeChanged - border box size changed from " << m_lastObservationSizes.borderBoxLogicalSize << " to " << currentSizes->borderBoxLogicalSize);
            return currentSizes;
        }
        break;
    case ResizeObserverBoxOptions::ContentBox:
        if (m_lastObservationSizes.contentBoxLogicalSize != currentSizes->contentBoxLogicalSize) {
            LOG_WITH_STREAM(ResizeObserver, stream << "ResizeObservation " << *this << " elementSizeChanged - content box size changed from " << m_lastObservationSizes.contentBoxLogicalSize << " to " << currentSizes->contentBoxLogicalSize);
            return currentSizes;
        }
        break;
    }

    return { };
}

// https://drafts.csswg.org/resize-observer/#calculate-depth-for-node
size_t ResizeObservation::targetElementDepth() const
{
    unsigned depth = 0;
    for (Element* ownerElement = m_target.get(); ownerElement; ownerElement = ownerElement->document().ownerElement()) {
        for (Element* parent = ownerElement; parent; parent = parent->parentElementInComposedTree())
            ++depth;
    }

    return depth;
}

TextStream& operator<<(TextStream& ts, const ResizeObservation& observation)
{
    ts.dumpProperty("target"_s, ValueOrNull(observation.target()));

    if (auto* box = observation.target()->renderBox())
        ts.dumpProperty("target box"_s, box);

    ts.dumpProperty("border box"_s, observation.borderBoxSize());
    ts.dumpProperty("content box"_s, observation.contentBoxSize());
    ts.dumpProperty("snapped content box"_s, observation.snappedContentBoxSize());
    return ts;
}

} // namespace WebCore
