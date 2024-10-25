/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#import <WebKit/WebNotification.h>
#import <WebKit/WebViewPrivate.h>
#import <wtf/HashMap.h>
#import <wtf/HashSet.h>
#import <wtf/RetainPtr.h>
#import <wtf/text/WTFString.h>

using NotificationIDMap = UncheckedKeyHashMap<String, RetainPtr<WebNotification>>;
using NotificationViewMap = UncheckedKeyHashMap<String, CFTypeRef>;

@interface MockWebNotificationProvider : NSObject <WebNotificationProvider> {
    HashSet<CFTypeRef> _registeredWebViews;
    NotificationIDMap _notifications;
    NotificationViewMap _notificationViewMap;
    RetainPtr<NSMutableDictionary> _permissions;
}

+ (MockWebNotificationProvider *)shared;

- (void)simulateWebNotificationClick:(NSString *)notificationID;
- (void)setWebNotificationOrigin:(NSString *)origin permission:(BOOL)allowed;
- (WebNotificationPermission)policyForOrigin:(WebSecurityOrigin *)origin;
- (void)removeAllWebNotificationPermissions;

- (void)reset;
@end
