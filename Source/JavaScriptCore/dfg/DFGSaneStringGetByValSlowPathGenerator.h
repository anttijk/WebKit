/*
 * Copyright (C) 2013-2024 Apple Inc. All rights reserved.
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

#if ENABLE(DFG_JIT)

#include "DFGOperations.h"
#include "DFGSlowPathGenerator.h"
#include "DFGSpeculativeJIT.h"
#include <wtf/Vector.h>

namespace JSC { namespace DFG {

class SaneStringGetByValSlowPathGenerator final : public JumpingSlowPathGenerator<MacroAssembler::Jump> {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(SaneStringGetByValSlowPathGenerator);
public:
    SaneStringGetByValSlowPathGenerator(
        const MacroAssembler::Jump& from, SpeculativeJIT* jit, JSValueRegs resultRegs, JITCompiler::LinkableConstant globalObject, GPRReg baseReg, GPRReg propertyReg)
        : JumpingSlowPathGenerator<MacroAssembler::Jump>(from, jit)
        , m_resultRegs(resultRegs)
        , m_globalObject(globalObject)
        , m_baseReg(baseReg)
        , m_propertyReg(propertyReg)
    {
        jit->silentSpillAllRegistersImpl(false, m_plans, extractResult(resultRegs));
    }
    
private:
    void generateInternal(SpeculativeJIT* jit) final
    {
        linkFrom(jit);
        
        MacroAssembler::Jump isNeg = jit->branch32(
            MacroAssembler::LessThan, m_propertyReg, MacroAssembler::TrustedImm32(0));
        
#if USE(JSVALUE64)
        jit->move(
            MacroAssembler::TrustedImm64(JSValue::encode(jsUndefined())), m_resultRegs.gpr());
#else
        jit->move(
            MacroAssembler::TrustedImm32(JSValue::UndefinedTag), m_resultRegs.tagGPR());
        jit->move(
            MacroAssembler::TrustedImm32(0), m_resultRegs.payloadGPR());
#endif
        jumpTo(jit);
        
        isNeg.link(jit);

        jit->callOperationWithSilentSpill(m_plans, operationGetByValStringInt, extractResult(m_resultRegs), m_globalObject, m_baseReg, m_propertyReg);
        
        jumpTo(jit);
    }

    JSValueRegs m_resultRegs;
    JITCompiler::LinkableConstant m_globalObject;
    GPRReg m_baseReg;
    GPRReg m_propertyReg;
    Vector<SilentRegisterSavePlan, 2> m_plans;
};

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)
