/*
 * Copyright (C) 2026 Apple Inc. All rights reserved.
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "StyleCalcSizeValue+Interpolation.h"

#include "StyleCalcSizeValue.h"
#include "StyleCalculationTree+Copy.h"
#include "StyleCalculationTree+Interpolation.h"

namespace WebCore {
namespace Style {

static Calculation::Tree hundredPercent()
{
    return Calculation::Tree { .root = Calculation::percentage(100) };
}

// https://drafts.csswg.org/css-values-5/#simplifying-calc-size
std::optional<PreparedCalcSize> prepareForInterpolation(const CalcSizeValue& value)
{
    return WTF::switchOn(value.basis(),
        [&](const Ref<CalcSizeValue>& nested) -> std::optional<PreparedCalcSize> {
            // The outer function takes the inner basis, and the inner calculation stands in for the
            // outer `size`.
            auto inner = prepareForInterpolation(nested);
            if (!inner)
                return { };
            auto calculation = Calculation::substituteSize(value.calculation(), inner->calculation.root);
            if (!calculation)
                return { };
            return PreparedCalcSize { inner->basis, WTF::move(*calculation) };
        },
        [&](const Calculation::Tree& basis) -> std::optional<PreparedCalcSize> {
            if (!value.basisHasPercentage()) {
                // A length basis is a definite size, so the function no longer depends on a basis.
                auto calculation = Calculation::substituteSize(value.calculation(), basis.root);
                if (!calculation)
                    return { };
                return PreparedCalcSize { { .kind = PreparedCalcSizeBasis::Kind::Any }, WTF::move(*calculation) };
            }

            // A percentage basis has to stay a percentage so it still resolves against the containing
            // block. De-percentifying restates it in terms of the 100% it becomes.
            auto dePercentified = Calculation::dePercentify(basis);
            auto calculation = Calculation::substituteSize(value.calculation(), dePercentified.root);
            if (!calculation)
                return { };
            return PreparedCalcSize { { .kind = PreparedCalcSizeBasis::Kind::HundredPercent }, WTF::move(*calculation) };
        },
        [&](const CSS::Keyword::Any&) -> std::optional<PreparedCalcSize> {
            return PreparedCalcSize { { .kind = PreparedCalcSizeBasis::Kind::Any }, Calculation::copy(value.calculation()) };
        },
        [&]<CSSValueID Id>(const Constant<Id>&) -> std::optional<PreparedCalcSize> {
            return PreparedCalcSize { { .kind = PreparedCalcSizeBasis::Kind::Keyword, .keyword = Id }, Calculation::copy(value.calculation()) };
        }
    );
}

// https://drafts.csswg.org/css-values-5/#interpolate-calc-size
std::optional<PreparedCalcSizeBasis> interpolatedBasis(const PreparedCalcSizeBasis& a, const PreparedCalcSizeBasis& b)
{
    if (a == b)
        return a;
    if (a.kind == PreparedCalcSizeBasis::Kind::Any)
        return b;
    if (b.kind == PreparedCalcSizeBasis::Kind::Any)
        return a;

    // Two different bases would each want the function to act a different way.
    return { };
}

// https://drafts.csswg.org/css-values-5/#interpolate-size
bool canCombineWithSizeKeyword(const CalcSizeValue& value, CSSValueID keyword)
{
    return WTF::switchOn(value.basis(),
        [&](const CSS::Keyword::Any&) -> bool {
            // `any` states that the value does not depend on a basis, so it takes on the keyword.
            return true;
        },
        [&](const Calculation::Tree&) -> bool {
            return false;
        },
        [&](const Ref<CalcSizeValue>& nested) -> bool {
            return canCombineWithSizeKeyword(nested, keyword);
        },
        [&]<CSSValueID Id>(const Constant<Id>&) -> bool {
            return Id == keyword;
        }
    );
}

RefPtr<CalcSizeValue> makeCalcSizeValue(const PreparedCalcSizeBasis& preparedBasis, Calculation::Tree&& calculation)
{
    auto basisHasPercentage = preparedBasis.kind == PreparedCalcSizeBasis::Kind::HundredPercent;
    auto hasPercentage = basisHasPercentage || Calculation::containsPercentage(calculation);

    auto basis = [&] -> std::optional<CalcSizeValue::Basis> {
        switch (preparedBasis.kind) {
        case PreparedCalcSizeBasis::Kind::Any:
            return CSS::Keyword::Any { };
        case PreparedCalcSizeBasis::Kind::HundredPercent:
            return hundredPercent();
        case PreparedCalcSizeBasis::Kind::Keyword:
            break;
        }

        switch (preparedBasis.keyword) {
        case CSSValueAuto:
            return CSS::Keyword::Auto { };
        case CSSValueContent:
            return CSS::Keyword::Content { };
        case CSSValueMinContent:
            return CSS::Keyword::MinContent { };
        case CSSValueMaxContent:
            return CSS::Keyword::MaxContent { };
        case CSSValueFitContent:
            return CSS::Keyword::FitContent { };
        case CSSValueStretch:
            return CSS::Keyword::Stretch { };
        case CSSValueWebkitFillAvailable:
            return CSS::Keyword::WebkitFillAvailable { };
        case CSSValueIntrinsic:
            return CSS::Keyword::Intrinsic { };
        case CSSValueMinIntrinsic:
            return CSS::Keyword::MinIntrinsic { };
        default:
            return { };
        }
    }();

    if (!basis)
        return { };

    return CalcSizeValue::create(WTF::move(*basis), WTF::move(calculation), basisHasPercentage, hasPercentage);
}

} // namespace Style
} // namespace WebCore
