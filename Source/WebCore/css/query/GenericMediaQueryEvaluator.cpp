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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "GenericMediaQueryEvaluator.h"

#include "CSSPrimitiveValue.h"
#include "CSSRatioValue.h"
#include "CSSToLengthConversionData.h"
#include "StylePrimitiveNumericTypes+Conversions.h"
#include "StyleRatio.h"

namespace WebCore {
namespace MQ {

static std::optional<LayoutUnit> computeLength(const CSSValue* value, const CSSToLengthConversionData& conversionData)
{
    auto* primitiveValue = dynamicDowncast<CSSPrimitiveValue>(value);
    if (!primitiveValue)
        return { };

    if (primitiveValue->isNumberOrInteger()) {
        if (primitiveValue->resolveAsNumber(conversionData))
            return { };
        return 0_lu;
    }

    if (!primitiveValue->isLength())
        return { };
    return primitiveValue->resolveAsLength<LayoutUnit>(conversionData);
}

template<typename T>
bool compare(ComparisonOperator op, T left, T right)
{
    switch (op) {
    case ComparisonOperator::LessThan:
        return left < right;
    case ComparisonOperator::GreaterThan:
        return left > right;
    case ComparisonOperator::LessThanOrEqual:
        return left <= right;
    case ComparisonOperator::GreaterThanOrEqual:
        return left >= right;
    case ComparisonOperator::Equal:
        return left == right;
    }
    RELEASE_ASSERT_NOT_REACHED();
};

enum class Side : uint8_t { Left, Right };
static EvaluationResult evaluateLengthComparison(LayoutUnit size, const std::optional<Comparison>& comparison, Side side, const CSSToLengthConversionData& conversionData)
{
    if (!comparison)
        return EvaluationResult::True;

    auto expressionSize = computeLength(RefPtr { comparison->value }.get(), conversionData);
    if (!expressionSize)
        return EvaluationResult::Unknown;

    auto left = side == Side::Left ? *expressionSize : size;
    auto right = side == Side::Left ? size : *expressionSize;

    return toEvaluationResult(compare(comparison->op, left, right));
};

static EvaluationResult evaluateNumberComparison(double number, const std::optional<Comparison>& comparison, Side side, const CSSToLengthConversionData& conversionData)
{
    if (!comparison)
        return EvaluationResult::True;

    auto expressionNumber = Ref { downcast<CSSPrimitiveValue>(*comparison->value) }->resolveAsNumber(conversionData);

    auto left = side == Side::Left ? expressionNumber : number;
    auto right = side == Side::Left ? number : expressionNumber;

    return toEvaluationResult(compare(comparison->op, left, right));
};

static EvaluationResult evaluateIntegerComparison(int number, const std::optional<Comparison>& comparison, Side side, const CSSToLengthConversionData& conversionData)
{
    if (!comparison)
        return EvaluationResult::True;

    auto expressionNumber = Ref { downcast<CSSPrimitiveValue>(*comparison->value) }->resolveAsInteger(conversionData);

    auto left = side == Side::Left ? expressionNumber : number;
    auto right = side == Side::Left ? number : expressionNumber;

    return toEvaluationResult(compare(comparison->op, left, right));
};

static EvaluationResult evaluateResolutionComparison(float resolution, const std::optional<Comparison>& comparison, Side side, const CSSToLengthConversionData& conversionData)
{
    if (!comparison)
        return EvaluationResult::True;

    auto expressionResolution = Ref { downcast<CSSPrimitiveValue>(*comparison->value) }->resolveAsResolution<float>(conversionData);

    auto left = side == Side::Left ? expressionResolution : resolution;
    auto right = side == Side::Left ? resolution : expressionResolution;

    return toEvaluationResult(compare(comparison->op, left, right));
};

EvaluationResult evaluateLengthFeature(const Feature& feature, LayoutUnit length, const CSSToLengthConversionData& conversionData)
{
    if (!feature.leftComparison && !feature.rightComparison)
        return toEvaluationResult(!!length);

    auto leftResult = evaluateLengthComparison(length, feature.leftComparison, Side::Left, conversionData);
    auto rightResult = evaluateLengthComparison(length, feature.rightComparison, Side::Right, conversionData);

    return leftResult & rightResult;
};

static EvaluationResult evaluateRatioComparison(FloatSize size, const std::optional<Comparison>& comparison, Side side, const CSSToLengthConversionData& conversionData)
{
    if (!comparison)
        return EvaluationResult::True;

    RefPtr ratioValue = dynamicDowncast<CSSRatioValue>(comparison->value);
    if (!ratioValue)
        return EvaluationResult::Unknown;

    auto resolvedRatio = Style::toStyle(ratioValue->ratio(), conversionData);

    // Ratio with zero denominator is infinite and compares greater to any value.

    auto comparisonA = resolvedRatio.denominator.value ? size.height() * resolvedRatio.numerator.value : 1.0f;
    auto comparisonB = resolvedRatio.denominator.value ? size.width() * resolvedRatio.denominator.value : 0.0f;

    auto left = side == Side::Left ? comparisonA : comparisonB;
    auto right = side == Side::Left ? comparisonB : comparisonA;

    return toEvaluationResult(compare(comparison->op, left, right));
};

EvaluationResult evaluateRatioFeature(const Feature& feature, FloatSize size, const CSSToLengthConversionData& conversionData)
{
    if (!feature.leftComparison && !feature.rightComparison)
        return toEvaluationResult(size.width());

    auto leftResult = evaluateRatioComparison(size, feature.leftComparison, Side::Left, conversionData);
    auto rightResult = evaluateRatioComparison(size, feature.rightComparison, Side::Right, conversionData);

    return leftResult & rightResult;
}

EvaluationResult evaluateBooleanFeature(const Feature& feature, bool currentValue, const CSSToLengthConversionData& conversionData)
{
    if (!feature.rightComparison)
        return toEvaluationResult(currentValue);

    Ref value = downcast<CSSPrimitiveValue>(*feature.rightComparison->value);
    auto expectedValue = value->resolveAsInteger(conversionData);

    if (expectedValue && expectedValue != 1)
        return EvaluationResult::Unknown;

    return toEvaluationResult(expectedValue == currentValue);
}

EvaluationResult evaluateIntegerFeature(const Feature& feature, int currentValue, const CSSToLengthConversionData& conversionData)
{
    if (!feature.leftComparison && !feature.rightComparison)
        return toEvaluationResult(!!currentValue);

    auto leftResult = evaluateIntegerComparison(currentValue, feature.leftComparison, Side::Left, conversionData);
    auto rightResult = evaluateIntegerComparison(currentValue, feature.rightComparison, Side::Right, conversionData);

    return leftResult & rightResult;
}

EvaluationResult evaluateNumberFeature(const Feature& feature, double currentValue, const CSSToLengthConversionData& conversionData)
{
    if (!feature.leftComparison && !feature.rightComparison)
        return toEvaluationResult(!!currentValue);

    auto leftResult = evaluateNumberComparison(currentValue, feature.leftComparison, Side::Left, conversionData);
    auto rightResult = evaluateNumberComparison(currentValue, feature.rightComparison, Side::Right, conversionData);

    return leftResult & rightResult;
}

EvaluationResult evaluateResolutionFeature(const Feature& feature, float currentValue, const CSSToLengthConversionData& conversionData)
{
    if (!feature.leftComparison && !feature.rightComparison)
        return toEvaluationResult(!!currentValue);

    auto leftResult = evaluateResolutionComparison(currentValue, feature.leftComparison, Side::Left, conversionData);
    auto rightResult = evaluateResolutionComparison(currentValue, feature.rightComparison, Side::Right, conversionData);

    return leftResult & rightResult;
}

EvaluationResult evaluateIdentifierFeature(const Feature& feature, CSSValueID currentValue, const CSSToLengthConversionData&)
{
    if (!feature.rightComparison)
        return toEvaluationResult(currentValue != CSSValueNone && currentValue != CSSValueNoPreference);

    auto& value = downcast<CSSPrimitiveValue>(*feature.rightComparison->value);
    return toEvaluationResult(value.valueID() == currentValue);
}

} // namespace MQ
} // namespace WebCore
