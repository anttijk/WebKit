/*
 * Copyright (C) 2012-2025 Apple Inc. All rights reserved.
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

#include <wtf/text/SymbolImpl.h>
#include <wtf/text/WTFString.h>

namespace JSC {

class PrivateName {
public:
    explicit PrivateName(SymbolImpl& uid)
        : m_uid(uid)
    {
    }

    enum DescriptionTag { Description };
    explicit PrivateName(DescriptionTag, const String& description)
        : m_uid(SymbolImpl::create(*description.impl()))
    {
    }

    enum PrivateSymbolTag { PrivateSymbol };
    explicit PrivateName(PrivateSymbolTag, const String& description)
        : m_uid(PrivateSymbolImpl::create(*description.impl()))
    {
    }

    PrivateName(const PrivateName& privateName)
        : m_uid(privateName.m_uid.copyRef())
    {
    }

    PrivateName(PrivateName&&) = default;

    SymbolImpl& uid() const { return m_uid; }

    bool operator==(const PrivateName& other) const { return &uid() == &other.uid(); }

private:
    const Ref<SymbolImpl> m_uid;
};

} // namespace JSC
