/*
 * Copyright (C) 2023-2025 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(MEDIA_STREAM)

#include "ActiveDOMObject.h"
#include "Blob.h"
#include "Document.h"
#include "JSDOMPromiseDeferred.h"
#include "MediaStreamTrack.h"
#include "PhotoCapabilities.h"
#include "PhotoSettings.h"

namespace WTF {
class Logger;
}

namespace WebCore {

class ImageBitmap;
class ImageCaptureVideoFrameObserver;

class ImageCapture : public RefCounted<ImageCapture>, public ActiveDOMObject, public MediaStreamTrackPrivateObserver {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(ImageCapture);
public:
    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    static ExceptionOr<Ref<ImageCapture>> create(Document&, Ref<MediaStreamTrack>);

    ~ImageCapture();

    void takePhoto(PhotoSettings&&, DOMPromiseDeferred<IDLInterface<Blob>>&&);
    void grabFrame(DOMPromiseDeferred<IDLInterface<ImageBitmap>>&&);
    void getPhotoCapabilities(DOMPromiseDeferred<IDLDictionary<PhotoCapabilities>>&&);
    void getPhotoSettings(DOMPromiseDeferred<IDLDictionary<PhotoSettings>>&&);

    Ref<MediaStreamTrack> track() const { return m_track; }

private:
    ImageCapture(Document&, Ref<MediaStreamTrack>);

    void stopGrabFrameObserver();

    // ActiveDOMObject
    void stop() final { stopGrabFrameObserver(); };

    // MediaStreamTrackPrivateObserver
    void trackEnded(MediaStreamTrackPrivate&) final;
    void trackMutedChanged(MediaStreamTrackPrivate&) final { }
    void trackSettingsChanged(MediaStreamTrackPrivate&) final { }
    void trackEnabledChanged(MediaStreamTrackPrivate&) final { }

#if !RELEASE_LOG_DISABLED
    const Logger& logger() const { return m_logger.get(); }
    uint64_t logIdentifier() const { return m_logIdentifier; }
    ASCIILiteral logClassName() const { return "ImageCapture"_s; }
    WTFLogChannel& logChannel() const;
#endif

    const Ref<MediaStreamTrack> m_track;
    RefPtr<ImageCaptureVideoFrameObserver> m_grabFrameObserver;
#if !RELEASE_LOG_DISABLED
    const Ref<const Logger> m_logger;
    const uint64_t m_logIdentifier;
#endif
};

}

#endif
