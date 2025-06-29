/*
 * Copyright (C) 2021-2025 Apple Inc. All rights reserved.
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

#include "Connection.h"
#include "Download.h"
#include "NetworkConnectionToWebProcess.h"
#include "NetworkDataTask.h"
#include <WebCore/FetchIdentifier.h>
#include <wtf/FileHandle.h>
#include <wtf/FunctionDispatcher.h>
#include <wtf/TZoneMalloc.h>

namespace IPC {
class FormDataReference;
class SharedBufferReference;
}

namespace WebKit {

class NetworkLoad;
class NetworkProcess;
class SandboxExtension;
class WebSWServerToContextConnection;

class ServiceWorkerDownloadTask : public NetworkDataTask, private FunctionDispatcher, private IPC::MessageReceiver {
    WTF_MAKE_TZONE_ALLOCATED(ServiceWorkerDownloadTask);
public:
    static Ref<ServiceWorkerDownloadTask> create(NetworkSession& session, NetworkDataTaskClient& client, WebSWServerToContextConnection& connection, WebCore::ServiceWorkerIdentifier serviceWorkerIdentifier, WebCore::SWServerConnectionIdentifier serverConnectionIdentifier, WebCore::FetchIdentifier fetchIdentifier, const WebCore::ResourceRequest& request, const WebCore::ResourceResponse& response, DownloadID downloadID)
    {
        auto task = adoptRef(*new ServiceWorkerDownloadTask(session, client, connection, serviceWorkerIdentifier, serverConnectionIdentifier, fetchIdentifier, request, response, downloadID));
        task->startListeningForIPC();
        return task;
    }
    ~ServiceWorkerDownloadTask();

    void ref() const final { NetworkDataTask::ref(); }
    void deref() const final { NetworkDataTask::deref(); }

    WebCore::FetchIdentifier fetchIdentifier() const { return m_fetchIdentifier; }
    void contextClosed() { cancel(); }
    void start();
    void stop() { cancel(); }

private:
    ServiceWorkerDownloadTask(NetworkSession&, NetworkDataTaskClient&, WebSWServerToContextConnection&, WebCore::ServiceWorkerIdentifier, WebCore::SWServerConnectionIdentifier, WebCore::FetchIdentifier, const WebCore::ResourceRequest&, const WebCore::ResourceResponse& response, DownloadID);
    void startListeningForIPC();

    // IPC Message
    void didReceiveData(const IPC::SharedBufferReference&);
    void didReceiveFormData(const IPC::FormDataReference&);
    void didFinish();
    void didFail(WebCore::ResourceError&&);

    // NetworkDataTask
    void cancel() final;
    void resume() final;
    void invalidateAndCancel() final;
    State state() const final { return m_state; }
    void setPendingDownloadLocation(const String& filename, SandboxExtension::Handle&&, bool /*allowOverwrite*/) final;

    // FunctionDispatcher
    void dispatch(Function<void()>&&) final;

    // IPC::MessageReceiver
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) final;

    template<typename Message> bool sendToServiceWorker(Message&&);
    void didFailDownload(std::optional<WebCore::ResourceError>&& = { });
    void close();

    WeakPtr<WebSWServerToContextConnection> m_serviceWorkerConnection;
    WebCore::ServiceWorkerIdentifier m_serviceWorkerIdentifier;
    WebCore::SWServerConnectionIdentifier m_serverConnectionIdentifier;
    WebCore::FetchIdentifier m_fetchIdentifier;
    DownloadID m_downloadID;
    const Ref<NetworkProcess> m_networkProcess;
    RefPtr<SandboxExtension> m_sandboxExtension;
    FileSystem::FileHandle m_downloadFile;
    uint64_t m_downloadBytesWritten { 0 };
    std::optional<uint64_t> m_expectedContentLength;
    State m_state { State::Suspended };
};

}
