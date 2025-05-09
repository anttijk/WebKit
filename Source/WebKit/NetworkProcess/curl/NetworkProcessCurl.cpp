/*
 * Copyright (C) 2017 Sony Interactive Entertainment Inc.
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
#include "NetworkProcess.h"

#include "NetworkCache.h"
#include "NetworkProcessCreationParameters.h"
#include "NetworkSessionCurl.h"
#include <WebCore/CurlContext.h>
#include <WebCore/NetworkStorageSession.h>
#include <WebCore/NotImplemented.h>
#include <wtf/CallbackAggregator.h>

namespace WebKit {

using namespace WebCore;

void NetworkProcess::platformInitializeNetworkProcess(const NetworkProcessCreationParameters&)
{
}

void NetworkProcess::allowSpecificHTTPSCertificateForHost(PAL::SessionID, const CertificateInfo& certificateInfo, const String& host)
{
    notImplemented();
}

void NetworkProcess::clearDiskCache(WallTime modifiedSince, CompletionHandler<void()>&& completionHandler)
{
    auto aggregator = CallbackAggregator::create(WTFMove(completionHandler));
    forEachNetworkSession([modifiedSince, &aggregator](NetworkSession& session) {
        if (auto* cache = session.cache())
            cache->clear(modifiedSince, [aggregator] () { });
    });
}

void NetworkProcess::platformTerminate()
{
    notImplemented();
}

void NetworkProcess::setNetworkProxySettings(PAL::SessionID sessionID, WebCore::CurlProxySettings&& settings)
{
    if (auto* networkStorageSession = storageSession(sessionID))
        networkStorageSession->setProxySettings(settings);
    else
        ASSERT_NOT_REACHED();
}

void NetworkProcess::setIgnoreTLSErrors(PAL::SessionID sessionID, bool ignoreTLSErrors)
{
    if (auto* session = networkSession(sessionID))
        static_cast<NetworkSessionCurl&>(*session).setIgnoreTLSErrors(ignoreTLSErrors);
}

} // namespace WebKit
