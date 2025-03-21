/*
 * Copyright (C) 2004, 2006, 2014 Apple Inc. All rights reserved.
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

#import "config.h"
#import "TextBoundaries.h"

#import <CoreFoundation/CFStringTokenizer.h>
#import <Foundation/Foundation.h>
#import <unicode/ubrk.h>
#import <unicode/uchar.h>
#import <unicode/ustring.h>
#import <unicode/utypes.h>
#import <wtf/RetainPtr.h>
#import <wtf/text/StringView.h>
#import <wtf/text/TextBreakIterator.h>
#import <wtf/text/TextBreakIteratorInternalICU.h>
#import <wtf/unicode/CharacterNames.h>

namespace WebCore {

#if !USE(APPKIT)

static bool isWordDelimitingCharacter(char32_t c)
{
    // Ampersand is an exception added to treat AT&T as a single word (see <rdar://problem/5022264>).
    return !CFCharacterSetIsLongCharacterMember(CFCharacterSetGetPredefined(kCFCharacterSetAlphaNumeric), c) && c != '&';
}

static bool isSymbolCharacter(char32_t c)
{
    return CFCharacterSetIsLongCharacterMember(CFCharacterSetGetPredefined(kCFCharacterSetSymbol), c);
}

static bool isAmbiguousBoundaryCharacter(char32_t character)
{
    // These are characters that can behave as word boundaries, but can appear within words.
    return character == '\'' || character == rightSingleQuotationMark || character == hebrewPunctuationGershayim;
}

static CFStringTokenizerRef tokenizerForString(CFStringRef str)
{
    static const NeverDestroyed locale = [] {
        const char* localID = currentTextBreakLocaleID();
        auto currentLocaleID = adoptCF(CFStringCreateWithCStringNoCopy(kCFAllocatorDefault, localID, kCFStringEncodingASCII, kCFAllocatorNull));
        return adoptCF(CFLocaleCreate(kCFAllocatorDefault, currentLocaleID.get()));
    }();

    if (!locale.get())
        return nullptr;

    CFRange entireRange = CFRangeMake(0, CFStringGetLength(str));    

    static NeverDestroyed<RetainPtr<CFStringTokenizerRef>> tokenizer;
    if (!tokenizer.get())
        tokenizer.get() = adoptCF(CFStringTokenizerCreate(kCFAllocatorDefault, str, entireRange, kCFStringTokenizerUnitWordBoundary, locale.get().get()));
    else
        CFStringTokenizerSetString(tokenizer.get().get(), str, entireRange);
    return tokenizer.get().get();
}

// Simple case: A word is a stream of characters delimited by a special set of word-delimiting characters.
static void findSimpleWordBoundary(StringView text, int position, int* start, int* end)
{
    ASSERT(position >= 0);
    ASSERT(static_cast<unsigned>(position) < text.length());

    unsigned startPos = position;
    while (startPos > 0) {
        int i = startPos;
        char32_t characterBeforeStartPos;
        U16_PREV(text, 0, i, characterBeforeStartPos);
        if (isWordDelimitingCharacter(characterBeforeStartPos)) {
            ASSERT(i >= 0);
            if (!i)
                break;

            if (!isAmbiguousBoundaryCharacter(characterBeforeStartPos))
                break;

            char32_t characterBeforeBeforeStartPos;
            U16_PREV(text, 0, i, characterBeforeBeforeStartPos);
            if (isWordDelimitingCharacter(characterBeforeBeforeStartPos))
                break;
        }
        U16_BACK_1(text, 0, startPos);
    }
    
    unsigned endPos = position;
    while (endPos < text.length()) {
        char32_t character;
        U16_GET(text, 0, endPos, text.length(), character);
        if (isWordDelimitingCharacter(character)) {
            unsigned i = endPos;
            U16_FWD_1(text, i, text.length());
            ASSERT(i <= text.length());
            if (i == text.length())
                break;
            char32_t characterAfterEndPos;
            U16_NEXT(text, i, text.length(), characterAfterEndPos);
            if (!isAmbiguousBoundaryCharacter(character))
                break;
            if (isWordDelimitingCharacter(characterAfterEndPos))
                break;
        }
        U16_FWD_1(text, endPos, text.length());
    }

    // The text may consist of all delimiter characters (e.g. "++++++++" or a series of emoji), and returning an empty range
    // makes no sense (and doesn't match findComplexWordBoundary() behavior).
    if (startPos == endPos && endPos < text.length()) {
        char32_t character;
        U16_GET(text, 0, endPos, text.length(), character);
        if (isSymbolCharacter(character))
            U16_FWD_1(text, endPos, text.length());
    }

    *start = startPos;
    *end = endPos;
}

// Complex case: use CFStringTokenizer to find word boundary.
static void findComplexWordBoundary(StringView text, int position, int* start, int* end)
{
    RetainPtr<CFStringRef> charString = text.createCFStringWithoutCopying();

    CFStringTokenizerRef tokenizer = tokenizerForString(charString.get());
    if (!tokenizer) {
        // Error creating tokenizer, so just use simple function.
        findSimpleWordBoundary(text, position, start, end);
        return;
    }

    CFStringTokenizerTokenType  token = CFStringTokenizerGoToTokenAtIndex(tokenizer, position);
    if (token == kCFStringTokenizerTokenNone) {
        // No token found: select entire block.
        // NB: I never hit this section in all my testing.
        *start = 0;
        *end = text.length();
        return;
    }

    CFRange result = CFStringTokenizerGetCurrentTokenRange(tokenizer);
    *start = result.location;
    *end = result.location + result.length;
}

#endif

void findWordBoundary(StringView text, int position, int* start, int* end)
{
#if USE(APPKIT)
    auto attributedString = adoptNS([[NSAttributedString alloc] initWithString:text.createNSStringWithoutCopying().get()]);
    NSRange range = [attributedString doubleClickAtIndex:std::min<unsigned>(position, text.length() - 1)];
    *start = range.location;
    *end = range.location + range.length;
#else
    if (text.isEmpty()) {
        *start = 0;
        *end = 0;
        return;
    }

    if (static_cast<unsigned>(position) >= text.length()) {
        ASSERT_WITH_MESSAGE(static_cast<unsigned>(position) < text.length(), "position exceeds text.length()");
        *start = text.length() - 1;
        *end = text.length() - 1;
        return;
    }

    // For complex text (Thai, Japanese, Chinese), visible_units will pass the text in as a 
    // single contiguous run of characters, providing as much context as is possible.
    // We only need one character to determine if the text is complex.
    char32_t ch;
    unsigned i = position;
    U16_NEXT(text, i, text.length(), ch);
    bool isComplex = requiresContextForWordBoundary(ch);

    // FIXME: This check improves our word boundary behavior, but doesn't actually go far enough.
    // See <rdar://problem/8853951> Take complex word boundary finding path when necessary
    if (!isComplex) {
        // Check again for complex text, at the start of the run.
        i = 0;
        U16_NEXT(text, i, text.length(), ch);
        isComplex = requiresContextForWordBoundary(ch);
    }

    if (isComplex)
        findComplexWordBoundary(text, position, start, end);
    else
        findSimpleWordBoundary(text, position, start, end);

#define LOG_WORD_BREAK 0
#if LOG_WORD_BREAK
    auto uniString = text.createCFStringWithoutCopying();
    auto foundWord = text.substring(*start, *end - *start).createCFStringWithoutCopying();
    NSLog(@"%s_BREAK '%@' (%d,%d) in '%@' (%p) at %d, length=%d", isComplex ? "COMPLEX" : "SIMPLE", foundWord.get(), *start, *end, uniString.get(), uniString.get(), position, text.length());
#endif
    
#endif
}

void findEndWordBoundary(StringView text, int position, int* end)
{
    int start;
    findWordBoundary(text, position, &start, end);
}

#if USE(APPKIT)

// FIXME: Is this special Mac implementation actually important, or can
// we share with all the other platforms?
int findNextWordFromIndex(StringView text, int position, bool forward)
{   
    String textWithoutUnpairedSurrogates;
    if (hasUnpairedSurrogate(text)) {
        textWithoutUnpairedSurrogates = replaceUnpairedSurrogatesWithReplacementCharacter(text.toStringWithoutCopying());
        text = textWithoutUnpairedSurrogates;
    }
    auto attributedString = adoptNS([[NSAttributedString alloc] initWithString:text.createNSStringWithoutCopying().get()]);
    return [attributedString nextWordFromIndex:position forward:forward];
}

#endif // USE(APPKIT)

}
