/*
 * Copyright (C) 2008-2017 Apple Inc. All Rights Reserved.
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

#include "EventTarget.h"
#include "EventTargetInterfaces.h"
#include "LocalDOMWindowProperty.h"
#include <wtf/WeakPtr.h>

namespace WebCore {

class ApplicationCacheHost;
class LocalFrame;

class DOMApplicationCache final : public RefCounted<DOMApplicationCache>, public EventTarget, public LocalDOMWindowProperty {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(DOMApplicationCache);
public:
    static Ref<DOMApplicationCache> create(LocalDOMWindow& window) { return adoptRef(*new DOMApplicationCache(window)); }
    virtual ~DOMApplicationCache() { ASSERT(!frame()); }

    unsigned short status() const;
    ExceptionOr<void> update();
    ExceptionOr<void> swapCache();
    void abort();

    using RefCounted::ref;
    using RefCounted::deref;

private:
    explicit DOMApplicationCache(LocalDOMWindow&);

    void refEventTarget() final { ref(); }
    void derefEventTarget() final { deref(); }

    enum EventTargetInterfaceType eventTargetInterface() const final { return EventTargetInterfaceType::DOMApplicationCache; }
    ScriptExecutionContext* scriptExecutionContext() const final;

    ApplicationCacheHost* applicationCacheHost() const;
};

} // namespace WebCore
