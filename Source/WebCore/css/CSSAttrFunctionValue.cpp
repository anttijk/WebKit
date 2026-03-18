/*
 * Copyright (C) 2024 Alexsander Borges Damaceno <alexbdamac@gmail.com>. All rights reserved.
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
#include "CSSAttrFunctionValue.h"

#include "CSSPrimitiveValue.h"
#include <wtf/text/MakeString.h>

namespace WebCore {

// MARK: - CSS::AttrFunction

namespace CSS {

bool AttrFunction::operator==(const AttrFunction& other) const
{
    RefPtr fallbackValue = dynamicDowncast<CSSPrimitiveValue>(fallback);
    RefPtr otherFallback = dynamicDowncast<CSSPrimitiveValue>(other.fallback);

    if (fallbackValue && otherFallback)
        return attributeName == other.attributeName && fallbackValue->stringValue() == otherFallback->stringValue();
    if (fallbackValue || otherFallback)
        return false;
    return attributeName == other.attributeName;
}

String AttrFunction::cssText(const SerializationContext& context) const
{
    RefPtr fallbackValue = dynamicDowncast<CSSPrimitiveValue>(fallback);
    return makeString(
        "attr("_s,
        attributeName.impl(),
        fallbackValue && !fallbackValue->stringValue().isEmpty() ? ", "_s : ""_s,
        fallbackValue && !fallbackValue->stringValue().isEmpty() ? fallbackValue->cssText(context) : ""_s,
        ')'
    );
}

} // namespace CSS

// MARK: - CSSAttrFunctionValue

Ref<CSSAttrFunctionValue> CSSAttrFunctionValue::create(CSS::AttrFunction attr)
{
    return adoptRef(*new CSSAttrFunctionValue(WTF::move(attr)));
}

CSSAttrFunctionValue::CSSAttrFunctionValue(CSS::AttrFunction attr)
    : CSSValue(ClassType::AttrFunction)
    , m_attr(WTF::move(attr))
{
}

String CSSAttrFunctionValue::customCSSText(const CSS::SerializationContext& context) const
{
    return m_attr.cssText(context);
}

bool CSSAttrFunctionValue::equals(const CSSAttrFunctionValue& other) const
{
    return m_attr == other.m_attr;
}

} // namespace WebCore
