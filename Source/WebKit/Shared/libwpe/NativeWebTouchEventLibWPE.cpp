/*
 * Copyright (C) 2014 Igalia S.L.
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
#include "NativeWebTouchEvent.h"

#if ENABLE(TOUCH_EVENTS)

#include "WebEventFactory.h"

namespace WebKit {

NativeWebTouchEvent::NativeWebTouchEvent(struct wpe_input_touch_event* event, float deviceScaleFactor)
    : WebTouchEvent(WebEventFactory::createWebTouchEvent(event, deviceScaleFactor))
    , m_fallbackTouchPoint { wpe_input_touch_event_type_null, 0, 0, 0, 0 }
{
    for (unsigned i = 0; i < event->touchpoints_length; ++i) {
        WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN // WPE port
        auto& point = event->touchpoints[i];
        WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
        if (point.type != wpe_input_touch_event_type_null) {
            m_fallbackTouchPoint = point;
            break;
        }
    }
}

} // namespace WebKit

#endif // PLATFORM(TOUCH_EVENTS)
