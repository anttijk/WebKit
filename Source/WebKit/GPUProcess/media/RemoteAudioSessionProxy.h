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

#pragma once

#if ENABLE(GPU_PROCESS) && USE(AUDIO_SESSION)

#include "MessageReceiver.h"
#include "RemoteAudioSessionConfiguration.h"
#include <WebCore/AudioSession.h>
#include <WebCore/ProcessIdentifier.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/ThreadSafeWeakPtr.h>
#include <wtf/WeakPtr.h>
#include <wtf/WeakRef.h>

namespace IPC {
class Connection;
}

namespace WebKit {

class GPUConnectionToWebProcess;
class RemoteAudioSessionProxyManager;
struct SharedPreferencesForWebProcess;

class RemoteAudioSessionProxy
    : public RefCounted<RemoteAudioSessionProxy>, public IPC::MessageReceiver {
    WTF_MAKE_TZONE_ALLOCATED(RemoteAudioSessionProxy);
public:
    static Ref<RemoteAudioSessionProxy> create(GPUConnectionToWebProcess& gpuConnection)
    {
        return adoptRef(*new RemoteAudioSessionProxy(gpuConnection));
    }

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    virtual ~RemoteAudioSessionProxy();

    WebCore::ProcessIdentifier processIdentifier();
    RemoteAudioSessionConfiguration configuration();

    WebCore::AudioSession::CategoryType category() const { return m_category; };
    WebCore::AudioSession::Mode mode() const { return m_mode; };
    WebCore::RouteSharingPolicy routeSharingPolicy() const { return m_routeSharingPolicy; }
    size_t preferredBufferSize() const { return m_preferredBufferSize; }
    bool isActive() const { return m_active; }
    bool isInterrupted() const { return m_isInterrupted; }

    void configurationChanged();
    void beginInterruption();
    void endInterruption(WebCore::AudioSession::MayResume);

    const String& sceneIdentifier() const { return m_sceneIdentifier; }
    void setSceneIdentifier(const String&);

    WebCore::AudioSession::SoundStageSize soundStageSize() const { return m_soundStageSize; }
    void setSoundStageSize(WebCore::AudioSession::SoundStageSize);

    // IPC::MessageReceiver
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) final;
    bool didReceiveSyncMessage(IPC::Connection&, IPC::Decoder&, UniqueRef<IPC::Encoder>&) final;

    RefPtr<GPUConnectionToWebProcess> gpuConnectionToWebProcess() const;
    std::optional<SharedPreferencesForWebProcess> sharedPreferencesForWebProcess() const;

#if PLATFORM(IOS_FAMILY)
    void setPreferredSpeakerID(const String&);
#endif

private:
    explicit RemoteAudioSessionProxy(GPUConnectionToWebProcess&);

    // Messages
    void setCategory(WebCore::AudioSession::CategoryType, WebCore::AudioSession::Mode, WebCore::RouteSharingPolicy);
    void setPreferredBufferSize(uint64_t);
    using SetActiveCompletion = CompletionHandler<void(bool)>;
    void tryToSetActive(bool, SetActiveCompletion&&);
    void setIsPlayingToBluetoothOverride(std::optional<bool>&& value);
    void triggerBeginInterruptionForTesting();
    void triggerEndInterruptionForTesting();

    void beginInterruptionRemote();
    void endInterruptionRemote(WebCore::AudioSession::MayResume);

    RemoteAudioSessionProxyManager& audioSessionManager();
    Ref<RemoteAudioSessionProxyManager> protectedAudioSessionManager();
    Ref<IPC::Connection> protectedConnection() const;

    ThreadSafeWeakPtr<GPUConnectionToWebProcess> m_gpuConnection;
    WebCore::AudioSession::CategoryType m_category { WebCore::AudioSession::CategoryType::None };
    WebCore::AudioSession::Mode m_mode { WebCore::AudioSession::Mode::Default };
    WebCore::RouteSharingPolicy m_routeSharingPolicy { WebCore::RouteSharingPolicy::Default };
    WebCore::AudioSession::SoundStageSize m_soundStageSize { WebCore::AudioSession::SoundStageSize::Automatic };
    String m_sceneIdentifier;
    size_t m_preferredBufferSize { 0 };
    bool m_active { false };
    bool m_isInterrupted { false };
    bool m_isPlayingToBluetoothOverrideChanged { false };
#if PLATFORM(IOS_FAMILY)
    String m_speakerID;
#endif
};

}

#endif
