/*
 * Copyright (C) 2025 Apple Inc. All rights reserved.
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
#include "MatchResultCache.h"

#include "MatchResult.h"
#include "ResolvedStyle.h"
#include "StyleProperties.h"
#include "StyledElement.h"
#include <wtf/BitSet.h>

namespace WebCore {
namespace Style {

struct MatchResultCache::Entry {
    MatchResultAndStyle matchResultAndStyle;

    WTF_MAKE_STRUCT_FAST_ALLOCATED;
};

MatchResultCache::MatchResultCache() = default;
MatchResultCache::~MatchResultCache() = default;

inline std::unique_ptr<RenderStyle> copyStyle(auto& style)
{
    if (!style)
        return { };
    return RenderStyle::clonePtr(*style);
};

inline MatchResultAndStyle copyImmutable(const MatchResultAndStyle& other)
{
    auto resultCopy = makeUnique<MatchResult>(*other.matchResult);
    for (auto& declaration : resultCopy->authorDeclarations) {
        if (auto* mutableProperties = dynamicDowncast<MutableStyleProperties>(declaration.properties.get()))
            declaration.properties = mutableProperties->immutableCopy();
    };

    return {
        .matchResult = WTFMove(resultCopy),
        .unadjustedStyle = copyStyle(other.unadjustedStyle),
        .userAgentAppearanceStyle = copyStyle(other.userAgentAppearanceStyle),
        .relations = other.relations ? makeUnique<Relations>(*other.relations) : std::unique_ptr<Relations> { }
    };
}

inline MatchResultAndStyle copyReplacingInlineStyle(const MatchResultAndStyle& cachedResult, const StyleProperties& inlineStyle, size_t inlineStyleIndex)
{
    auto resultCopy = makeUnique<MatchResult>(*cachedResult.matchResult);

    auto& inlineStyleDeclaration = resultCopy->authorDeclarations[inlineStyleIndex];
    ASSERT(inlineStyleDeclaration.fromStyleAttribute == FromStyleAttribute::Yes);
    inlineStyleDeclaration.properties = inlineStyle.immutableCopyIfNeeded();

    return {
        .matchResult = WTFMove(resultCopy),
        .unadjustedStyle = copyStyle(cachedResult.unadjustedStyle),
        .userAgentAppearanceStyle = copyStyle(cachedResult.userAgentAppearanceStyle),
        .relations = cachedResult.relations ? makeUnique<Relations>(*cachedResult.relations) : std::unique_ptr<Relations> { }
    };
}

static PropertyCascade::IncludedProperties computeChangedProperties(const StyleProperties& from, const StyleProperties& to)
{

    WTF::BitSet<numCSSProperties> ids;
    for (auto property : to) {
        // FIXME: Support custom properties.
        if (property.id() == CSSPropertyCustom)
            return PropertyCascade::normalProperties();
        ids.set(property.id());
    }

    for (auto property : from) {
        // FIXME: Support removal of properties with partial application.
        if (!ids.get(property.id()))
            return PropertyCascade::normalProperties();
    }

    PropertyCascade::IncludedProperties result;
    for (auto index : ids) {
        auto propertyID = static_cast<CSSPropertyID>(index);
        auto fromValue = from.getPropertyCSSValue(propertyID);
        auto toValue = to.getPropertyCSSValue(propertyID);
        if (!fromValue || !toValue || *fromValue != *toValue) {
            // Low-priority properties are safe to apply by themselves as no other property may depend on them.
            // FIXME: CSSPropertyLineHeight should be high-priority as other properties may depend on it via 'lh' unit.
            if (propertyID < firstLowPriorityProperty || propertyID == CSSPropertyLineHeight)
                return PropertyCascade::normalProperties();
            result.ids.append(propertyID);
        }
    }
    return result;
}

const std::optional<CachedMatchResult> MatchResultCache::resultWithCurrentInlineStyle(const Element& element)
{
    auto it = m_entries.find(element);
    if (it == m_entries.end())
        return { };

    auto& matchResultAndStyle = it->value->matchResultAndStyle;

    auto* styledElement = dynamicDowncast<StyledElement>(element);
    auto* inlineStyle = styledElement ? styledElement->inlineStyle() : nullptr;

    if (!inlineStyle) {
        m_entries.remove(it);
        return { };
    }

    auto inlineStyleIndex = [&]() -> std::optional<size_t> {
        size_t index = 0;
        for (auto& declaration : matchResultAndStyle.matchResult->authorDeclarations) {
            if (declaration.fromStyleAttribute == FromStyleAttribute::Yes)
                return index;
            ++index;
        }
        return { };
    }();

    if (!inlineStyleIndex) {
        m_entries.remove(it);
        return { };
    }

    auto changedProperties = computeChangedProperties(matchResultAndStyle.matchResult->authorDeclarations[*inlineStyleIndex].properties, *inlineStyle);

    auto updatedMatchResult = copyReplacingInlineStyle(matchResultAndStyle,  *inlineStyle, *inlineStyleIndex);

    return CachedMatchResult {
        .matchResultAndStyle = WTFMove(updatedMatchResult),
        .changedProperties = WTFMove(changedProperties)
    };
}

void MatchResultCache::update(const Element& element, const MatchResultAndStyle& matchResultAndStyle)
{
    // For now we cache match results if there is mutable inline style. This way we can avoid
    // selector matching when it gets mutated again.
    auto* styledElement = dynamicDowncast<StyledElement>(element);
    if (styledElement && styledElement->inlineStyle() && styledElement->inlineStyle()->isMutable()) {
        m_entries.set(element, makeUniqueRef<Entry>(Entry {
            .matchResultAndStyle = copyImmutable(matchResultAndStyle)
        }));
    } else
        m_entries.remove(element);
}

}
}
