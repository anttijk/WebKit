/*
 * Copyright (C) 2010-2020 Apple Inc. All rights reserved.
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

#include "ArgumentCoders.h"
#include "MessageNames.h"
#include <wtf/StdLibExtras.h>

namespace IPC {

template<typename, typename> struct ArgumentCoder;

// IPC encoder which:
//  - Encodes to a caller buffer with fixed size, does not resize, stops when size runs out
//  - Does not initialize alignment gaps
//
class StreamConnectionEncoder final {
public:
    // Stream allocation needs to be at least size of StreamSetDestinationID message at any offset % messageAlignment.
    // StreamSetDestinationID has MessageName+uint64_t, where uint64_t is expected to to be aligned at 8.
    static constexpr size_t minimumMessageSize = 16;
    static constexpr size_t messageAlignment = alignof(MessageName);
    static constexpr bool isIPCEncoder = true;

    StreamConnectionEncoder(MessageName messageName, std::span<uint8_t> stream)
        : m_buffer(stream)
    {
        // Instead of using *this << messageName, manually encode the messageName since we
        // know the stream buffer is bigger than needed as per API contract.
        auto bytes = asBytes(singleElementSpan(messageName));
        ASSERT(stream.size() > bytes.size()); // By API contract.
        memcpySpan(m_buffer, bytes);
        m_encodedSize = bytes.size();
    }

    ~StreamConnectionEncoder() = default;

    template<typename T, size_t Extent>
    bool encodeSpan(std::span<T, Extent> span)
    {
        auto bytes = asBytes(span);
        auto bufferPointer = reinterpret_cast<uintptr_t>(m_buffer.data()) + m_encodedSize;
        auto newBufferPointer = roundUpToMultipleOf<alignof(T)>(bufferPointer);
        if (newBufferPointer < bufferPointer)
            return false;
        auto alignedSize = m_encodedSize + (newBufferPointer - bufferPointer);
        if (!reserve(alignedSize, bytes.size()))
            return false;
        memcpySpan(m_buffer.subspan(alignedSize), bytes);
        m_encodedSize = alignedSize + bytes.size();
        return true;
    }

    template<typename T>
    bool encodeObject(const T& object)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return encodeSpan(singleElementSpan(object));
    }

    template<typename T>
    StreamConnectionEncoder& operator<<(T&& t)
    {
        ArgumentCoder<std::remove_cvref_t<T>, void>::encode(*this, std::forward<T>(t));
        return *this;
    }

    size_t size() const { ASSERT(isValid()); return m_encodedSize; }
    bool isValid() const { return !!m_buffer.data(); }
    operator bool() const { return isValid(); }
private:
    bool reserve(size_t alignedSize, size_t additionalSize)
    {
        size_t size = alignedSize + additionalSize;
        if (size < alignedSize || size > m_buffer.size()) {
            m_buffer = { };
            return false;
        }
        return true;
    }
    std::span<uint8_t> m_buffer;
    size_t m_encodedSize { 0 };
};

} // namespace IPC
