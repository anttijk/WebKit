/*
 * Copyright (C) 2014, 2015 Apple Inc. All rights reserved.
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

#if ENABLE(FTL_JIT)

#include "DFGNodeType.h"
#include "FTLExitPropertyValue.h"
#include "FTLExitValue.h"
#include <wtf/Noncopyable.h>

namespace JSC {

class TrackedReferences;

namespace FTL {

class ExitTimeObjectMaterialization {
    WTF_MAKE_NONCOPYABLE(ExitTimeObjectMaterialization)
public:
    ExitTimeObjectMaterialization(DFG::Node*, CodeOrigin);
    ~ExitTimeObjectMaterialization();
    
    void add(DFG::PromotedLocationDescriptor, const ExitValue&);

    DFG::NodeType type() const { return m_type; }
    IndexingType indexingType() const { return m_indexingType; }
    size_t size() const { return m_size; }
    CodeOrigin origin() const { return m_origin; }
    
    ExitValue get(DFG::PromotedLocationDescriptor) const;
    const Vector<ExitPropertyValue>& properties() const { return m_properties; }
    
    void accountForLocalsOffset(int offset);
    
    void dump(PrintStream& out) const;
    
    void validateReferences(const TrackedReferences&) const;
    
private:
    DFG::NodeType m_type;
    IndexingType m_indexingType;
    size_t m_size;
    CodeOrigin m_origin;
    Vector<ExitPropertyValue> m_properties;
};

} } // namespace JSC::FTL

#endif // ENABLE(FTL_JIT)
