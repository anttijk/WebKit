// Copyright 2015 The Chromium Authors. All rights reserved.
// Copyright (C) 2016-2021 Apple Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//    * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "config.h"
#include "CSSVariableParser.h"

#include "CSSCustomPropertyValue.h"
#include "CSSParserContext.h"
#include "CSSParserIdioms.h"
#include "CSSParserTokenRange.h"
#include "CSSPropertyParser.h"
#include "CSSPropertyParserConsumer+Primitives.h"
#include "CSSTokenizer.h"
#include "CSSValueKeywords.h"
#include "StyleCustomProperty.h"
#include <stack>

namespace WebCore {

bool CSSVariableParser::isValidVariableName(const CSSParserToken& token)
{
    if (token.type() != IdentToken)
        return false;

    return isCustomPropertyName(token.value());
}

static bool isValidConstantName(const CSSParserToken& token)
{
    return token.type() == IdentToken;
}

static bool isValidVariableReference(CSSParserTokenRange, const CSSParserContext&);
static bool isValidConstantReference(CSSParserTokenRange, const CSSParserContext&);

static bool classifyBlock(CSSParserTokenRange range, bool& hasReferences, bool& hasTopLevelBraceBlockMixedWithOtherValues, const CSSParserContext& parserContext)
{
    struct ClassifyBlockState {
        CSSParserTokenRange range;
        bool isTopLevelBlock = true;
        bool hasOtherValues = false;
        unsigned topLevelBraceBlocks = 0;
        bool doneWithThisRange = false;
    };
    ClassifyBlockState initialState { .range = range };

    std::stack<ClassifyBlockState> stack;
    stack.push(initialState);

    while (!stack.empty()) {
        auto& current = stack.top();
        if (current.doneWithThisRange) {
            // If there is a top level brace block, the value should contains only that.
            if (current.topLevelBraceBlocks > 1 || (current.topLevelBraceBlocks == 1 && current.hasOtherValues))
                hasTopLevelBraceBlockMixedWithOtherValues = true;
            stack.pop();
            continue;
        }

        if (current.range.atEnd()) {
            current.doneWithThisRange = true;
            continue;
        }

        if (current.isTopLevelBlock) {
            auto tokenType = current.range.peek().type();
            if (!CSSTokenizer::isWhitespace(tokenType)) {
                if (tokenType == LeftBraceToken)
                    current.topLevelBraceBlocks++;
                else
                    current.hasOtherValues = true;
            }
        }

        if (current.range.peek().getBlockType() == CSSParserToken::BlockStart) {
            const CSSParserToken& token = current.range.peek();
            CSSParserTokenRange block = current.range.consumeBlock();
            if (token.functionId() == CSSValueVar) {
                if (!isValidVariableReference(block, parserContext))
                    return false; // Bail if any references are invalid
                hasReferences = true;
                continue;
            }
            if (token.functionId() == CSSValueEnv) {
                if (!isValidConstantReference(block, parserContext))
                    return false; // Bail if any references are invalid
                hasReferences = true;
                continue;
            }
            stack.push(ClassifyBlockState {
                .range = block,
                .isTopLevelBlock = false, // Nested block, not top-level
            });
            continue;
        }

        ASSERT(current.range.peek().getBlockType() != CSSParserToken::BlockEnd);

        const CSSParserToken& token = current.range.consume();
        switch (token.type()) {
        case AtKeywordToken:
            break;
        case DelimiterToken: {
            if (token.delimiter() == '!' && current.isTopLevelBlock)
                return false;
            break;
        }
        case RightParenthesisToken:
        case RightBraceToken:
        case RightBracketToken:
        case BadStringToken:
        case BadUrlToken:
            return false;
        case SemicolonToken:
            if (current.isTopLevelBlock)
                return false;
            break;
        default:
            break;
        }

    }

    return true;
}

bool isValidVariableReference(CSSParserTokenRange range, const CSSParserContext& parserContext)
{
    range.consumeWhitespace();
    if (!CSSVariableParser::isValidVariableName(range.consumeIncludingWhitespace()))
        return false;
    if (range.atEnd())
        return true;

    if (!CSSPropertyParserHelpers::consumeCommaIncludingWhitespace(range))
        return false;
    if (range.atEnd())
        return true;

    bool hasReferences = false;
    bool hasTopLevelBraceBlock = false;
    return classifyBlock(range, hasReferences, hasTopLevelBraceBlock, parserContext);
}

bool isValidConstantReference(CSSParserTokenRange range, const CSSParserContext& parserContext)
{
    range.consumeWhitespace();
    if (!isValidConstantName(range.consumeIncludingWhitespace()))
        return false;
    if (range.atEnd())
        return true;

    if (!CSSPropertyParserHelpers::consumeCommaIncludingWhitespace(range))
        return false;
    if (range.atEnd())
        return true;

    bool hasReferences = false;
    bool hasTopLevelBraceBlock = false;
    return classifyBlock(range, hasReferences, hasTopLevelBraceBlock, parserContext);
}

struct VariableType {
    std::optional<CSSWideKeyword> cssWideKeyword { };
    bool hasReferences { false };
    bool hasTopLevelBraceBlockWithOtherValues { false };
};

static std::optional<VariableType> classifyVariableRange(CSSParserTokenRange range, const CSSParserContext& parserContext)
{
    range.consumeWhitespace();

    if (range.peek().type() == IdentToken) {
        auto rangeCopy = range;
        CSSValueID id = range.consumeIncludingWhitespace().id();
        if (auto keyword = parseCSSWideKeyword(id); range.atEnd() && keyword)
            return VariableType { *keyword };
        // No fast path, restart with the complete range.
        range = rangeCopy;
    }

    VariableType type;
    if (!classifyBlock(range, type.hasReferences, type.hasTopLevelBraceBlockWithOtherValues, parserContext))
        return { };

    return type;
}

bool CSSVariableParser::containsValidVariableReferences(CSSParserTokenRange range, const CSSParserContext& parserContext)
{
    auto type = classifyVariableRange(range, parserContext);
    if (!type)
        return false;

    return type->hasReferences && !type->hasTopLevelBraceBlockWithOtherValues;
}

RefPtr<CSSCustomPropertyValue> CSSVariableParser::parseDeclarationValue(const AtomString& variableName, CSSParserTokenRange range, const CSSParserContext& parserContext)
{
    if (range.atEnd())
        return nullptr;

    auto type = classifyVariableRange(range, parserContext);

    if (!type)
        return nullptr;

    if (type->cssWideKeyword)
        return CSSCustomPropertyValue::createWithCSSWideKeyword(variableName, *type->cssWideKeyword);

    if (type->hasReferences)
        return CSSCustomPropertyValue::createUnresolved(variableName, CSSVariableReferenceValue::create(range, parserContext));

    return CSSCustomPropertyValue::createSyntaxAll(variableName, CSSVariableData::create(range, parserContext));
}

RefPtr<const Style::CustomProperty> CSSVariableParser::parseInitialValueForUniversalSyntax(const AtomString& variableName, CSSParserTokenRange range)
{
    if (range.atEnd())
        return nullptr;

    auto type = classifyVariableRange(range, strictCSSParserContext());

    if (!type || type->cssWideKeyword || type->hasReferences)
        return nullptr;

    return Style::CustomProperty::createForVariableData(variableName, CSSVariableData::create(range));
}

} // namespace WebCore
