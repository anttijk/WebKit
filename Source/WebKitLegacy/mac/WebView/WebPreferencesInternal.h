/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
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

#import "WebPreferencesPrivate.h"

@interface WebPreferences (WebPreferencesInternal)

- (BOOL)_boolValueForKey:(NSString *)key;
- (void)_setBoolValue:(BOOL)value forKey:(NSString *)key;
- (int)_integerValueForKey:(NSString *)key;
- (void)_setIntegerValue:(int)value forKey:(NSString *)key;
- (float)_floatValueForKey:(NSString *)key;
- (void)_setFloatValue:(float)value forKey:(NSString *)key;
- (void)_setLongLongValue:(long long)value forKey:(NSString *)key;
- (long long)_longLongValueForKey:(NSString *)key;
- (void)_setUnsignedLongLongValue:(unsigned long long)value forKey:(NSString *)key;
- (unsigned long long)_unsignedLongLongValueForKey:(NSString *)key;
- (NSString *)_stringValueForKey:(NSString *)key;
- (void)_setStringValue:(NSString *)value forKey:(NSString *)key;

@property (nonatomic, setter=_setUseSystemAppearance:) BOOL _useSystemAppearance;

@end
