/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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

#include "config.h"
#include "DFGAdaptiveStructureWatchpoint.h"

#if ENABLE(DFG_JIT)

#include "CodeBlockInlines.h"
#include "JSCellInlines.h"

namespace JSC { namespace DFG {

AdaptiveStructureWatchpoint::AdaptiveStructureWatchpoint(const ObjectPropertyCondition& key, CodeBlock* codeBlock)
    : Watchpoint(Watchpoint::Type::AdaptiveStructure)
    , m_codeBlock(codeBlock)
    , m_key(key)
{
    RELEASE_ASSERT(key.watchingRequiresStructureTransitionWatchpoint());
    RELEASE_ASSERT(!key.watchingRequiresReplacementWatchpoint());
}

AdaptiveStructureWatchpoint::AdaptiveStructureWatchpoint()
    : Watchpoint(Watchpoint::Type::AdaptiveStructure)
    , m_codeBlock(nullptr)
{
}

void AdaptiveStructureWatchpoint::initialize(const ObjectPropertyCondition& key, CodeBlock* codeBlock)
{
    m_codeBlock = codeBlock;
    m_key = key;
    RELEASE_ASSERT(key.watchingRequiresStructureTransitionWatchpoint());
    RELEASE_ASSERT(!key.watchingRequiresReplacementWatchpoint());
}

void AdaptiveStructureWatchpoint::install(VM&)
{
    RELEASE_ASSERT(m_key.isWatchable(PropertyCondition::MakeNoChanges));
    
    m_key.object()->structure()->addTransitionWatchpoint(this);
}

void AdaptiveStructureWatchpoint::fireInternal(VM& vm, const FireDetail& detail)
{
    ASSERT(!m_codeBlock->wasDestructed());
    if (m_codeBlock->isPendingDestruction())
        return;

    if (m_key.isWatchable(PropertyCondition::EnsureWatchability)) {
        install(vm);
        return;
    }
    
    dataLogLnIf(DFG::shouldDumpDisassembly(), "Firing watchpoint ", RawPointer(this), " (", m_key, ") on ", *m_codeBlock);

    auto lambda = scopedLambda<void(PrintStream&)>([&](PrintStream& out) {
        out.print("Adaptation of ", m_key, " failed: ", detail);
    });
    LazyFireDetail lazyDetail(lambda);
    m_codeBlock->jettison(Profiler::JettisonDueToUnprofiledWatchpoint, CountReoptimization, &lazyDetail);
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)

