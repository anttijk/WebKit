/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
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

#import "config.h"
#import <wtf/text/TextStream.h>

#import <CoreFoundation/CoreFoundation.h>

TEST(WTF_TextStream, CFString)
{
    TextStream ts;
    ts << reinterpret_cast<id>(const_cast<CFMutableStringRef>(CFSTR("Test")));
    EXPECT_EQ(ts.release(), "Test"_s);
}

TEST(WTF_TextStream, Hex)
{
    TextStream ts;
    ts << hex(31);
    EXPECT_EQ(ts.release(), "1F"_s);
}

TEST(WTF_TextStream, Span)
{
    TextStream ts;
    int values[] = { 1, 2, 3, 4 };
    std::span<int> valuesAsSpan(values);
    ts << valuesAsSpan;
    EXPECT_EQ(ts.release(), "[1, 2, 3, 4]"_s);
    std::span<int, 2> valuesAsSpan2 = valuesAsSpan.subspan<1, 2>();
    TextStream ts2;
    ts2 << valuesAsSpan2;
    EXPECT_EQ(ts2.release(), "[2, 3]"_s);
}
