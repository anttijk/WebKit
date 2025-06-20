/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
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
#include "ScriptTelemetryCategory.h"

#include "AdvancedPrivacyProtections.h"
#include <wtf/URL.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

ASCIILiteral description(ScriptTelemetryCategory category)
{
    switch (category) {
    case ScriptTelemetryCategory::Unspecified:
        ASSERT_NOT_REACHED();
        return "Unspecified"_s;
    case ScriptTelemetryCategory::Audio:
        return "Audio"_s;
    case ScriptTelemetryCategory::Canvas:
        return "Canvas"_s;
    case ScriptTelemetryCategory::Cookies:
        return "Cookies"_s;
    case ScriptTelemetryCategory::HardwareConcurrency:
        return "HardwareConcurrency"_s;
    case ScriptTelemetryCategory::LocalStorage:
        return "LocalStorage"_s;
    case ScriptTelemetryCategory::Payments:
        return "Payments"_s;
    case ScriptTelemetryCategory::QueryParameters:
        return "QueryParameters"_s;
    case ScriptTelemetryCategory::Referrer:
        return "Referrer"_s;
    case ScriptTelemetryCategory::ScreenOrViewport:
        return "ScreenOrViewport"_s;
    case ScriptTelemetryCategory::Speech:
        return "Speech"_s;
    case ScriptTelemetryCategory::FormControls:
        return "FormControls"_s;
    }
    ASSERT_NOT_REACHED();
    return { };
}

bool shouldEnableScriptTelemetry(ScriptTelemetryCategory category, OptionSet<AdvancedPrivacyProtections> protections)
{
    if (protections.contains(AdvancedPrivacyProtections::BaselineProtections))
        return true;

    return category != ScriptTelemetryCategory::FormControls;
}

String makeLogMessage(const URL& url, ScriptTelemetryCategory category)
{
#if ENABLE(SCRIPT_TELEMETRY)
    if (category == ScriptTelemetryCategory::Cookies)
        return makeString("Prevented "_s, url.string(), " from setting long-lived cookies"_s);
    return makeString("Prevented "_s, url.string(), " from accessing "_s, description(category));
#else
    return makeString(url.string(), " tried to access "_s, description(category));
#endif
}

} // namespace WebCore
