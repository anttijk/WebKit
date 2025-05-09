/*
 * Copyright (C) 2013-2025 Apple Inc. All rights reserved.
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
#include "FrameLoadState.h"

namespace WebKit {

FrameLoadState::~FrameLoadState()
{
}

void FrameLoadState::addObserver(FrameLoadStateObserver& observer)
{
    auto result = m_observers.add(observer);
    ASSERT_UNUSED(result, result.isNewEntry);
}

void FrameLoadState::removeObserver(FrameLoadStateObserver& observer)
{
    auto result = m_observers.remove(observer);
    ASSERT_UNUSED(result, result);
}

void FrameLoadState::didStartProvisionalLoad(URL&& url)
{
    ASSERT(m_provisionalURL.isEmpty());

    m_state = State::Provisional;
    m_provisionalURL = WTFMove(url);
    const URL& urlForObserver = m_provisionalURL;

    forEachObserver([&urlForObserver](FrameLoadStateObserver& observer) {
        observer.didReceiveProvisionalURL(urlForObserver);
        observer.didStartProvisionalLoad(urlForObserver);
    });
}

void FrameLoadState::didSuspend()
{
    m_state = State::Finished;
    m_provisionalURL = { };

    forEachObserver([](FrameLoadStateObserver& observer) {
        observer.didCancelProvisionalLoad();
    });
}

void FrameLoadState::didExplicitOpen(URL&& url)
{
    ASSERT(!url.isNull());
    m_provisionalURL = { };
    setURL(WTFMove(url));
}

void FrameLoadState::didReceiveServerRedirectForProvisionalLoad(URL&& url)
{
    ASSERT(m_state == State::Provisional);

    m_provisionalURL = WTFMove(url);
    const URL& urlForObserver = m_provisionalURL;

    forEachObserver([&urlForObserver](FrameLoadStateObserver& observer) {
        observer.didReceiveProvisionalURL(urlForObserver);
    });
}

void FrameLoadState::didFailProvisionalLoad()
{
    ASSERT(m_state == State::Provisional);

    m_state = State::Finished;
    m_provisionalURL = { };
    m_unreachableURL = m_lastUnreachableURL;

    forEachObserver([&](FrameLoadStateObserver& observer) {
        observer.didCancelProvisionalLoad();
        observer.didFailProvisionalLoad(m_unreachableURL);
    });
}

void FrameLoadState::didCommitLoad()
{
    ASSERT(m_state == State::Provisional);

    m_state = State::Committed;
    ASSERT(!m_provisionalURL.isNull());
    m_url = m_provisionalURL.isNull() ? aboutBlankURL() : std::exchange(m_provisionalURL, { });

    forEachObserver([&](FrameLoadStateObserver& observer) {
        observer.didCommitProvisionalLoad();
        observer.didCommitProvisionalLoad(m_isMainFrame);
    });
}

void FrameLoadState::didFinishLoad()
{
    ASSERT(m_state == State::Committed);
    ASSERT(m_provisionalURL.isEmpty());

    m_state = State::Finished;

    forEachObserver([&](FrameLoadStateObserver& observer) {
        observer.didFinishLoad(m_isMainFrame, m_url);
    });
}

void FrameLoadState::didFailLoad()
{
    ASSERT(m_state == State::Committed);
    ASSERT(m_provisionalURL.isEmpty());

    m_state = State::Finished;
    forEachObserver([&](FrameLoadStateObserver& observer) {
        observer.didFailLoad(m_url);
    });
}

void FrameLoadState::didSameDocumentNotification(URL&& url)
{
    ASSERT(!url.isNull());
    setURL(url.isNull() ? URL { aboutBlankURL() } : WTFMove(url));
}

void FrameLoadState::setURL(URL&& url)
{
    m_url = WTFMove(url);
    const URL& urlForObserver = m_url;
    forEachObserver([&urlForObserver](FrameLoadStateObserver& observer) {
        observer.didCancelProvisionalLoad();
        observer.didReceiveProvisionalURL(urlForObserver);
        observer.didCommitProvisionalLoad();
    });
}

void FrameLoadState::setUnreachableURL(const URL& unreachableURL)
{
    m_lastUnreachableURL = m_unreachableURL;
    m_unreachableURL = unreachableURL;
}

void FrameLoadState::forEachObserver(NOESCAPE const Function<void(FrameLoadStateObserver&)>& callback)
{
    m_observers.forEach([&callback](FrameLoadStateObserver& observer) {
        callback(Ref { observer });
    });
}

} // namespace WebKit
