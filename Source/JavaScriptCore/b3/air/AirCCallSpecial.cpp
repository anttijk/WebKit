/*
 * Copyright (C) 2015-2023 Apple Inc. All rights reserved.
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
#include "AirCCallSpecial.h"

#if ENABLE(B3_JIT)

#include "CCallHelpers.h"
#include <wtf/TZoneMallocInlines.h>

namespace JSC { namespace B3 { namespace Air {

WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED_IMPL(CCallSpecial);

CCallSpecial::CCallSpecial(bool isSIMDContext)
    : m_isSIMDContext(isSIMDContext)
{
    m_clobberedRegs = RegisterSetBuilder::registersToSaveForCCall(m_isSIMDContext ? RegisterSetBuilder::allRegisters() : RegisterSetBuilder::allScalarRegisters());
    m_clobberedRegs.remove(GPRInfo::returnValueGPR);
    m_clobberedRegs.remove(GPRInfo::returnValueGPR2);
    m_clobberedRegs.remove(FPRInfo::returnValueFPR);
}

CCallSpecial::~CCallSpecial() = default;

void CCallSpecial::forEachArg(Inst& inst, const ScopedLambda<Inst::EachArgCallback>& callback)
{
    for (unsigned i = 0; i < numCalleeArgs; ++i)
        callback(inst.args[calleeArgOffset + i], Arg::Use, GP, pointerWidth());
    for (unsigned i = 0; i < numReturnGPArgs; ++i)
        callback(inst.args[returnGPArgOffset + i], Arg::Def, GP, pointerWidth());
    for (unsigned i = 0; i < numReturnFPArgs; ++i)
        callback(inst.args[returnFPArgOffset + i], Arg::Def, FP, m_isSIMDContext ? conservativeWidth(FP) : conservativeWidthWithoutVectors(FP));
    
    for (unsigned i = argArgOffset; i < inst.args.size(); ++i) {
        // For the type, we can just query the arg's bank. The arg will have a bank, because we
        // require these args to be argument registers.
        Bank bank = inst.args[i].bank();
        callback(inst.args[i], Arg::Use, bank, m_isSIMDContext ? conservativeWidth(bank) : conservativeWidthWithoutVectors(bank));
    }
}

bool CCallSpecial::isValid(Inst& inst)
{
    if (inst.args.size() < argArgOffset)
        return false;

    for (unsigned i = 0; i < numCalleeArgs; ++i) {
        Arg& arg = inst.args[i + calleeArgOffset];
        if (!arg.isGP())
            return false;
        switch (arg.kind()) {
        case Arg::Imm:
            if (is32Bit())
                break;
            return false;
        case Arg::BigImm:
            if (is64Bit())
                break;
            return false;
        case Arg::Tmp:
        case Arg::Addr:
        case Arg::ExtendedOffsetAddr:
        case Arg::Stack:
        case Arg::CallArg:
            break;
        default:
            return false;
        }
    }

    // Return args need to be exact.
    if (inst.args[returnGPArgOffset + 0] != Tmp(GPRInfo::returnValueGPR))
        return false;
    if (inst.args[returnGPArgOffset + 1] != Tmp(GPRInfo::returnValueGPR2))
        return false;
    if (inst.args[returnFPArgOffset + 0] != Tmp(FPRInfo::returnValueFPR))
        return false;

    for (unsigned i = argArgOffset; i < inst.args.size(); ++i) {
        if (!inst.args[i].isReg())
            return false;

        if (inst.args[i] == Tmp(scratchRegister))
            return false;
    }
    return true;
}

bool CCallSpecial::admitsStack(Inst&, unsigned argIndex)
{
    // The callee can be on the stack unless targeting ARM64, where we can't later properly
    // handle an Addr callee argument in generate() due to disallowed scratch register usage.
    if (argIndex == calleeArgOffset)
        return !isARM64();
    
    return false;
}

bool CCallSpecial::admitsExtendedOffsetAddr(Inst& inst, unsigned argIndex)
{
    return admitsStack(inst, argIndex);
}

void CCallSpecial::reportUsedRegisters(Inst&, const RegisterSetBuilder&)
{
}

CCallHelpers::Jump CCallSpecial::generate(Inst& inst, CCallHelpers& jit, GenerationContext&)
{
    switch (inst.args[calleeArgOffset].kind()) {
    case Arg::Imm:
    case Arg::BigImm:
        jit.move(inst.args[calleeArgOffset].asTrustedImmPtr(), scratchRegister);
        jit.call(scratchRegister, OperationPtrTag);
        break;
    case Arg::Tmp:
        jit.call(inst.args[calleeArgOffset].gpr(), OperationPtrTag);
        break;
    case Arg::Addr:
    case Arg::ExtendedOffsetAddr:
        jit.call(inst.args[calleeArgOffset].asAddress(), OperationPtrTag);
        break;
    default:
        RELEASE_ASSERT_NOT_REACHED();
        break;
    }
    return CCallHelpers::Jump();
}

RegisterSetBuilder CCallSpecial::extraEarlyClobberedRegs(Inst&)
{
    return { };
}

RegisterSetBuilder CCallSpecial::extraClobberedRegs(Inst&)
{
    return m_clobberedRegs;
}

void CCallSpecial::dumpImpl(PrintStream& out) const
{
    out.print("CCall");
}

void CCallSpecial::deepDumpImpl(PrintStream& out) const
{
    out.print("function call that uses the C calling convention.");
}

} } } // namespace JSC::B3::Air

#endif // ENABLE(B3_JIT)
