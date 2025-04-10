/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
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

#if ENABLE(GPU_PROCESS)
#include "PrepareBackingStoreBuffersData.h"

#include <wtf/text/TextStream.h>

namespace WebKit {

TextStream& operator<<(TextStream& ts, const ImageBufferSetPrepareBufferForDisplayInputData& inputData)
{
    ts << "remoteImageBufferSet: "_s << inputData.remoteBufferSet;
    ts << " dirtyRegion: "_s << inputData.dirtyRegion;
    ts << " supportsPartialRepaint: "_s << inputData.supportsPartialRepaint;
    ts << " hasEmptyDirtyRegion: "_s << inputData.hasEmptyDirtyRegion;
    ts << " requiresClearedPixels: "_s << inputData.requiresClearedPixels;
    return ts;
}

TextStream& operator<<(TextStream& ts, const ImageBufferSetPrepareBufferForDisplayOutputData& outputData)
{
    ts << "displayRequirement: "_s << outputData.displayRequirement;
    ts << "bufferCacheIdentifiers: "_s << outputData.bufferCacheIdentifiers;
    return ts;
}

TextStream& operator<<(TextStream& ts, SwapBuffersDisplayRequirement displayRequirement)
{
    switch (displayRequirement) {
    case SwapBuffersDisplayRequirement::NeedsFullDisplay: ts << "full display"_s; break;
    case SwapBuffersDisplayRequirement::NeedsNormalDisplay: ts << "normal display"_s; break;
    case SwapBuffersDisplayRequirement::NeedsNoDisplay: ts << "no display"_s; break;
    }

    return ts;
}

} // namespace WebKit

#endif // ENABLE(GPU_PROCESS) && PLATFORM(COCOA)
