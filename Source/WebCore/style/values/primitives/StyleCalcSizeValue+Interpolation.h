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

#pragma once

#include <WebCore/CSSValueKeywords.h>
#include <WebCore/StyleCalculationTree.h>
#include <optional>

namespace WebCore {
namespace Style {

class CalcSizeValue;

// The basis of a calc-size() that has been prepared for interpolation. Preparing reduces every basis
// to one of these three, which is what makes two of them comparable. Kept separate from
// CalcSizeValue::Basis because that holds a Calculation::Tree and so cannot be copied or compared.
struct PreparedCalcSizeBasis {
    enum class Kind : uint8_t { Any, Keyword, HundredPercent };

    Kind kind { Kind::Any };
    CSSValueID keyword { CSSValueInvalid };

    bool operator==(const PreparedCalcSizeBasis&) const = default;
};

struct PreparedCalcSize {
    PreparedCalcSizeBasis basis;
    Calculation::Tree calculation;
};

// Folds the basis into the calculation, leaving it a keyword, `any` or 100%, so that interpolating the
// calculations on their own stays linear. Fails if substitution grows the calculation past the limit.
std::optional<PreparedCalcSize> prepareForInterpolation(const CalcSizeValue&);

// The basis the result takes, or { } if the two cannot be interpolated at all.
std::optional<PreparedCalcSizeBasis> interpolatedBasis(const PreparedCalcSizeBasis&, const PreparedCalcSizeBasis&);

// Whether a bare <size-keyword> may combine with this function, which `interpolate-size: numeric-only`
// limits to a compatible one. Asked of the basis as authored, since preparing turns a length basis into
// `any`, which would let calc-size(50px, size) combine with every keyword.
bool canCombineWithSizeKeyword(const CalcSizeValue&, CSSValueID);

// Builds the interpolation result. Fails if the basis keyword is not one calc-size() accepts.
RefPtr<CalcSizeValue> makeCalcSizeValue(const PreparedCalcSizeBasis&, Calculation::Tree&&);

} // namespace Style
} // namespace WebCore
