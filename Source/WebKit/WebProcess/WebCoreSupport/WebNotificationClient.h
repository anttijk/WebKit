/*
 * Copyright (C) 2011, 2012 Apple Inc. All rights reserved.
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

#pragma once

#if ENABLE(NOTIFICATIONS)

#include <WebCore/NotificationClient.h>
#include <WebCore/SecurityOriginData.h>
#include <wtf/HashSet.h>
#include <wtf/TZoneMalloc.h>

namespace WebKit {

class WebPage;

class WebNotificationClient final : public WebCore::NotificationClient {
    WTF_MAKE_TZONE_ALLOCATED(WebNotificationClient);
public:
    WebNotificationClient(WebPage*);
    virtual ~WebNotificationClient();
    void clearNotificationPermissionState();

private:
    bool show(WebCore::ScriptExecutionContext&, WebCore::NotificationData&&, RefPtr<WebCore::NotificationResources>&&, CompletionHandler<void()>&&) final;
    void cancel(WebCore::NotificationData&&) final;
    void notificationObjectDestroyed(WebCore::NotificationData&&) final;
    void notificationControllerDestroyed() final;
    void requestPermission(WebCore::ScriptExecutionContext&, PermissionHandler&&) final;
    WebCore::NotificationClient::Permission checkPermission(WebCore::ScriptExecutionContext*) final;
    bool isWebKit2WebNotificationClient() const final { return true; }

    HashSet<WebCore::SecurityOriginData> m_notificationPermissionRequesters;
    WeakPtr<WebPage> m_page;
};

} // namespace WebKit

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::WebNotificationClient)
static bool isType(const WebCore::NotificationClient& client) { return client.isWebKit2WebNotificationClient(); }
SPECIALIZE_TYPE_TRAITS_END()

#endif // ENABLE(NOTIFICATIONS)
