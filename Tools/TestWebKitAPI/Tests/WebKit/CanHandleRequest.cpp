/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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

#if WK_HAVE_C_SPI

#include "PlatformUtilities.h"
#include "PlatformWebView.h"
#include <WebKit/WKContextPrivate.h>

namespace TestWebKitAPI {

static bool didReceiveMessage;
static bool canHandleRequest;

static void didReceiveMessageFromInjectedBundle(WKContextRef, WKStringRef messageName, WKTypeRef body, const void*)
{
    didReceiveMessage = true;

    EXPECT_WK_STREQ("DidCheckCanHandleRequest", messageName);
    EXPECT_EQ(WKBooleanGetTypeID(), WKGetTypeID(body));

    canHandleRequest = WKBooleanGetValue(static_cast<WKBooleanRef>(body));
}

static void setInjectedBundleClient(WKContextRef context)
{
    WKContextInjectedBundleClientV0 injectedBundleClient;
    zeroBytes(injectedBundleClient);

    injectedBundleClient.base.version = 0;
    injectedBundleClient.didReceiveMessageFromInjectedBundle = didReceiveMessageFromInjectedBundle;

    WKContextSetInjectedBundleClient(context, &injectedBundleClient.base);
}

TEST(WebKit, CanHandleRequest)
{
    WKRetainPtr<WKContextRef> context = adoptWK(Util::createContextForInjectedBundleTest("CanHandleRequestTest"));
    setInjectedBundleClient(context.get());

    WKContextRegisterURLSchemeAsEmptyDocument(context.get(), Util::toWK("emptyscheme").get());

    PlatformWebView webView(context.get());

    WKPageLoadURL(webView.page(), adoptWK(Util::createURLForResource("simple", "html")).get());

    WKContextPostMessageToInjectedBundle(context.get(), Util::toWK("CheckCanHandleRequest").get(), 0);
    Util::run(&didReceiveMessage);
    EXPECT_TRUE(canHandleRequest);
}

} // namespace TestWebKitAPI

#endif
