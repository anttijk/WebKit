/*
 * Copyright (C) 2020-2025 Apple Inc. All rights reserved.
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
#include "RemoteCDMFactory.h"

#if ENABLE(GPU_PROCESS) && ENABLE(ENCRYPTED_MEDIA)

#include "GPUProcessConnection.h"
#include "RemoteCDM.h"
#include "RemoteCDMFactoryProxyMessages.h"
#include "RemoteCDMInstanceSession.h"
#include "WebProcess.h"
#include <WebCore/Settings.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

using namespace WebCore;

WTF_MAKE_TZONE_ALLOCATED_IMPL(RemoteCDMFactory);

RemoteCDMFactory::RemoteCDMFactory(WebProcess& webProcess)
    : m_webProcess(webProcess)
{
}

RemoteCDMFactory::~RemoteCDMFactory() = default;

void RemoteCDMFactory::ref() const
{
    return m_webProcess->ref();
}

void RemoteCDMFactory::deref() const
{
    return m_webProcess->deref();
}

void RemoteCDMFactory::registerFactory(Vector<CDMFactory*>& factories)
{
    factories.append(this);
}

ASCIILiteral RemoteCDMFactory::supplementName()
{
    return "RemoteCDMFactory"_s;
}

GPUProcessConnection& RemoteCDMFactory::gpuProcessConnection()
{
    return WebProcess::singleton().ensureGPUProcessConnection();
}

bool RemoteCDMFactory::supportsKeySystem(const String& keySystem)
{
    auto sendResult = gpuProcessConnection().connection().sendSync(Messages::RemoteCDMFactoryProxy::SupportsKeySystem(keySystem), { });
    auto [supported] = sendResult.takeReplyOr(false);
    return supported;
}

std::unique_ptr<CDMPrivate> RemoteCDMFactory::createCDM(const String& keySystem, const String& mediaKeysHashSalt, const CDMPrivateClient&)
{
    auto sendResult = gpuProcessConnection().connection().sendSync(Messages::RemoteCDMFactoryProxy::CreateCDM(keySystem, mediaKeysHashSalt), { });
    auto [identifier, configuration] = sendResult.takeReplyOr(std::nullopt, RemoteCDMConfiguration { });
    if (!identifier)
        return nullptr;
    return RemoteCDM::create(*this, WTFMove(*identifier), WTFMove(configuration), mediaKeysHashSalt);
}

void RemoteCDMFactory::addSession(RemoteCDMInstanceSession& session)
{
    ASSERT(!m_sessions.contains(session.identifier()));
    m_sessions.set(session.identifier(), session);
}

void RemoteCDMFactory::removeSession(RemoteCDMInstanceSessionIdentifier identifier)
{
    ASSERT(m_sessions.contains(identifier));
    m_sessions.remove(identifier);
    gpuProcessConnection().connection().send(Messages::RemoteCDMFactoryProxy::RemoveSession(identifier), { });
}

void RemoteCDMFactory::removeInstance(RemoteCDMInstanceIdentifier identifier)
{
    gpuProcessConnection().connection().send(Messages::RemoteCDMFactoryProxy::RemoveInstance(identifier), { });
}

void RemoteCDMFactory::didReceiveSessionMessage(IPC::Connection& connection, IPC::Decoder& decoder)
{
    if (ObjectIdentifier<RemoteCDMInstanceSessionIdentifierType>::isValidIdentifier(decoder.destinationID())) {
        if (auto session = m_sessions.get(ObjectIdentifier<RemoteCDMInstanceSessionIdentifierType>(decoder.destinationID())))
            session->didReceiveMessage(connection, decoder);
    }
}

}

#endif
