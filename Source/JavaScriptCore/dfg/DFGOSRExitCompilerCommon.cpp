/*
 * Copyright (C) 2013-2022 Apple Inc. All rights reserved.
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
#include "DFGOSRExitCompilerCommon.h"

#if ENABLE(DFG_JIT)

#include "CodeBlockInlines.h"
#include "DFGJITCode.h"
#include "DFGOperations.h"
#include "JIT.h"
#include "JSCJSValueInlines.h"
#include "LLIntData.h"
#include "LLIntThunks.h"
#include "StructureStubInfo.h"

namespace JSC { namespace DFG {

void handleExitCounts(VM& vm, CCallHelpers& jit, const OSRExitBase& exit)
{
    if (!exitKindMayJettison(exit.m_kind)) {
        // FIXME: We may want to notice that we're frequently exiting
        // at an op_catch that we didn't compile an entrypoint for, and
        // then trigger a reoptimization of this CodeBlock:
        // https://bugs.webkit.org/show_bug.cgi?id=175842
        return;
    }

    jit.add32(AssemblyHelpers::TrustedImm32(1), AssemblyHelpers::AbsoluteAddress(&exit.m_count));
    
    jit.move(AssemblyHelpers::TrustedImmPtr(jit.codeBlock()), GPRInfo::regT3);
    
    CCallHelpers::Jump tooFewFails;
    CCallHelpers::JumpList doneAdjusting;
    
    jit.load32(AssemblyHelpers::Address(GPRInfo::regT3, CodeBlock::offsetOfOSRExitCounter()), GPRInfo::regT2);
    jit.add32(AssemblyHelpers::TrustedImm32(1), GPRInfo::regT2);
    jit.store32(GPRInfo::regT2, AssemblyHelpers::Address(GPRInfo::regT3, CodeBlock::offsetOfOSRExitCounter()));
    
    jit.move(AssemblyHelpers::TrustedImmPtr(jit.baselineCodeBlock()), GPRInfo::regT0);
    jit.loadPtr(AssemblyHelpers::Address(GPRInfo::regT0, CodeBlock::offsetOfJITData()), GPRInfo::regT5);

    auto isLLIntCodeBlock = jit.branchTestPtr(CCallHelpers::Zero, GPRInfo::regT5);
    AssemblyHelpers::Jump reoptimizeNow = jit.branch32(
        AssemblyHelpers::GreaterThanOrEqual,
        AssemblyHelpers::Address(GPRInfo::regT5, BaselineJITData::offsetOfJITExecuteCounter()),
        AssemblyHelpers::TrustedImm32(0));
    isLLIntCodeBlock.link(&jit);

    // We want to figure out if there's a possibility that we're in a loop. For the outermost
    // code block in the inline stack, we handle this appropriately by having the loop OSR trigger
    // check the exit count of the replacement of the CodeBlock from which we are OSRing. The
    // problem is the inlined functions, which might also have loops, but whose baseline versions
    // don't know where to look for the exit count. Figure out if those loops are severe enough
    // that we had tried to OSR enter. If so, then we should use the loop reoptimization trigger.
    // Otherwise, we should use the normal reoptimization trigger.
    
    AssemblyHelpers::JumpList loopThreshold;
    
    for (InlineCallFrame* inlineCallFrame = exit.m_codeOrigin.inlineCallFrame(); inlineCallFrame; inlineCallFrame = inlineCallFrame->directCaller.inlineCallFrame()) {
        loopThreshold.append(
            jit.branchTest8(
                AssemblyHelpers::NonZero,
                AssemblyHelpers::AbsoluteAddress(
                    inlineCallFrame->baselineCodeBlock->ownerExecutable()->addressOfDidTryToEnterInLoop())));
    }
    
    jit.move(
        AssemblyHelpers::TrustedImm32(jit.codeBlock()->exitCountThresholdForReoptimization()),
        GPRInfo::regT1);
    
    if (!loopThreshold.empty()) {
        AssemblyHelpers::Jump done = jit.jump();

        loopThreshold.link(&jit);
        jit.move(
            AssemblyHelpers::TrustedImm32(
                jit.codeBlock()->exitCountThresholdForReoptimizationFromLoop()),
            GPRInfo::regT1);
        
        done.link(&jit);
    }
    
    tooFewFails = jit.branch32(AssemblyHelpers::BelowOrEqual, GPRInfo::regT2, GPRInfo::regT1);
    
    reoptimizeNow.link(&jit);
    
    jit.setupArguments<decltype(operationTriggerReoptimizationNow)>(GPRInfo::regT0, GPRInfo::regT3, AssemblyHelpers::TrustedImmPtr(&exit));
    jit.prepareCallOperation(vm);
    jit.move(AssemblyHelpers::TrustedImmPtr(tagCFunction<OperationPtrTag>(operationTriggerReoptimizationNow)), GPRInfo::nonArgGPR0);
    jit.call(GPRInfo::nonArgGPR0, OperationPtrTag);
    doneAdjusting.append(jit.jump());

    tooFewFails.link(&jit);
    doneAdjusting.append(jit.branchTestPtr(CCallHelpers::Zero, GPRInfo::regT5));

    // Adjust the execution counter such that the target is to only optimize after a while.
    int32_t activeThreshold =
        jit.baselineCodeBlock()->adjustedCounterValue(
            Options::thresholdForOptimizeAfterLongWarmUp());
    int32_t targetValue = applyMemoryUsageHeuristicsAndConvertToInt(
        activeThreshold, jit.baselineCodeBlock());
    int32_t clippedValue;
    switch (jit.codeBlock()->jitType()) {
    case JITType::DFGJIT:
        clippedValue = BaselineExecutionCounter::clippedThreshold(jit.codeBlock(), targetValue);
        break;
    case JITType::FTLJIT:
        clippedValue = UpperTierExecutionCounter::clippedThreshold(jit.codeBlock(), targetValue);
        break;
    default:
        RELEASE_ASSERT_NOT_REACHED();
#if COMPILER_QUIRK(CONSIDERS_UNREACHABLE_CODE)
        clippedValue = 0; // Make some compilers, and mhahnenberg, happy.
#endif
        break;
    }
    jit.store32(AssemblyHelpers::TrustedImm32(-clippedValue), AssemblyHelpers::Address(GPRInfo::regT5, BaselineJITData::offsetOfJITExecuteCounter()));
    jit.store32(AssemblyHelpers::TrustedImm32(activeThreshold), AssemblyHelpers::Address(GPRInfo::regT5, BaselineJITData::offsetOfJITExecutionActiveThreshold()));
    jit.store32(AssemblyHelpers::TrustedImm32(formattedTotalExecutionCount(clippedValue)), AssemblyHelpers::Address(GPRInfo::regT5, BaselineJITData::offsetOfJITExecutionTotalCount()));
    
    doneAdjusting.link(&jit);
}

static CodePtr<JSEntryPtrTag> callerReturnPC(CodeBlock* baselineCodeBlockForCaller, BytecodeIndex callBytecodeIndex, InlineCallFrame::Kind trueCallerCallKind, bool& callerIsLLInt)
{
    callerIsLLInt = Options::forceOSRExitToLLInt() || baselineCodeBlockForCaller->jitType() == JITType::InterpreterThunk;

    if (callBytecodeIndex.checkpoint())
        return LLInt::checkpointOSRExitFromInlinedCallTrampolineThunk().code();

    CodePtr<JSEntryPtrTag> jumpTarget;

    const auto& callInstruction = *baselineCodeBlockForCaller->instructions().at(callBytecodeIndex).ptr();
    if (callerIsLLInt) {
#define LLINT_RETURN_LOCATION(name) LLInt::returnLocationThunk(name##_return_location, callInstruction.width()).code()

        switch (trueCallerCallKind) {
        case InlineCallFrame::Call:
        case InlineCallFrame::BoundFunctionCall: {
            if (callInstruction.opcodeID() == op_call)
                jumpTarget = LLINT_RETURN_LOCATION(op_call);
            else if (callInstruction.opcodeID() == op_call_ignore_result)
                jumpTarget = LLINT_RETURN_LOCATION(op_call_ignore_result);
            else if (callInstruction.opcodeID() == op_iterator_open)
                jumpTarget = LLINT_RETURN_LOCATION(op_iterator_open);
            else if (callInstruction.opcodeID() == op_iterator_next)
                jumpTarget = LLINT_RETURN_LOCATION(op_iterator_next);
            break;
        }
        case InlineCallFrame::Construct:
            if (callInstruction.opcodeID() == op_construct)
                jumpTarget = LLINT_RETURN_LOCATION(op_construct);
            else if (callInstruction.opcodeID() == op_super_construct)
                jumpTarget = LLINT_RETURN_LOCATION(op_super_construct);
            break;
        case InlineCallFrame::CallVarargs:
            jumpTarget = LLINT_RETURN_LOCATION(op_call_varargs);
            break;
        case InlineCallFrame::ConstructVarargs:
            if (callInstruction.opcodeID() == op_construct_varargs)
                jumpTarget = LLINT_RETURN_LOCATION(op_construct_varargs);
            else if (callInstruction.opcodeID() == op_super_construct_varargs)
                jumpTarget = LLINT_RETURN_LOCATION(op_super_construct_varargs);
            break;
        case InlineCallFrame::GetterCall:
        case InlineCallFrame::ProxyObjectLoadCall: {
            if (callInstruction.opcodeID() == op_get_by_id)
                jumpTarget = LLINT_RETURN_LOCATION(op_get_by_id);
            else if (callInstruction.opcodeID() == op_get_length)
                jumpTarget = LLINT_RETURN_LOCATION(op_get_length);
            else if (callInstruction.opcodeID() == op_get_by_id_direct)
                jumpTarget = LLINT_RETURN_LOCATION(op_get_by_id_direct);
            else if (callInstruction.opcodeID() == op_get_by_val)
                jumpTarget = LLINT_RETURN_LOCATION(op_get_by_val);
            else if (callInstruction.opcodeID() == op_enumerator_get_by_val)
                jumpTarget = LLINT_RETURN_LOCATION(op_enumerator_get_by_val);
            else
                RELEASE_ASSERT_NOT_REACHED();
            break;
        }
        case InlineCallFrame::SetterCall:
        case InlineCallFrame::ProxyObjectStoreCall: {
            if (callInstruction.opcodeID() == op_put_by_id)
                jumpTarget = LLINT_RETURN_LOCATION(op_put_by_id);
            else if (callInstruction.opcodeID() == op_put_by_val)
                jumpTarget = LLINT_RETURN_LOCATION(op_put_by_val);
            else if (callInstruction.opcodeID() == op_put_by_val_direct)
                jumpTarget = LLINT_RETURN_LOCATION(op_put_by_val_direct);
            else if (callInstruction.opcodeID() == op_enumerator_put_by_val)
                jumpTarget = LLINT_RETURN_LOCATION(op_enumerator_put_by_val);
            else
                RELEASE_ASSERT_NOT_REACHED();
            break;
        }
        case InlineCallFrame::ProxyObjectInCall: {
            if (callInstruction.opcodeID() == op_in_by_id)
                jumpTarget = LLINT_RETURN_LOCATION(op_in_by_id);
            else if (callInstruction.opcodeID() == op_in_by_val)
                jumpTarget = LLINT_RETURN_LOCATION(op_in_by_val);
            else if (callInstruction.opcodeID() == op_enumerator_in_by_val)
                jumpTarget = LLINT_RETURN_LOCATION(op_enumerator_in_by_val);
            else
                RELEASE_ASSERT_NOT_REACHED();
            break;
        }
        default:
            RELEASE_ASSERT_NOT_REACHED();
        }

#undef LLINT_RETURN_LOCATION

    } else {
        switch (trueCallerCallKind) {
        case InlineCallFrame::Call:
        case InlineCallFrame::Construct:
        case InlineCallFrame::CallVarargs:
        case InlineCallFrame::ConstructVarargs:
        case InlineCallFrame::BoundFunctionCall: {
            jumpTarget = static_cast<const BaselineJITCode*>(baselineCodeBlockForCaller->jitCode().get())->getCallLinkDoneLocationForBytecodeIndex(callBytecodeIndex).retagged<JSEntryPtrTag>();
            break;
        }

        case InlineCallFrame::GetterCall:
        case InlineCallFrame::SetterCall:
        case InlineCallFrame::ProxyObjectLoadCall:
        case InlineCallFrame::ProxyObjectStoreCall:
        case InlineCallFrame::ProxyObjectInCall: {
            StructureStubInfo* stubInfo = baselineCodeBlockForCaller->findStubInfo(CodeOrigin(callBytecodeIndex));
            RELEASE_ASSERT(stubInfo, callInstruction.opcodeID());
            jumpTarget = stubInfo->doneLocation.retagged<JSEntryPtrTag>();
            break;
        }

        default:
            RELEASE_ASSERT_NOT_REACHED();
        }
    }

    ASSERT(jumpTarget);
    return jumpTarget;
}

CCallHelpers::Address calleeSaveSlot(InlineCallFrame* inlineCallFrame, CodeBlock* baselineCodeBlock, GPRReg calleeSave)
{
    const RegisterAtOffsetList* calleeSaves = baselineCodeBlock->jitCode()->calleeSaveRegisters();
    for (unsigned i = 0; i < calleeSaves->registerCount(); i++) {
        RegisterAtOffset entry = calleeSaves->at(i);
        if (entry.reg() != calleeSave)
            continue;
        return CCallHelpers::Address(CCallHelpers::framePointerRegister, static_cast<VirtualRegister>(inlineCallFrame->stackOffset).offsetInBytes() + entry.offset());
    }

    RELEASE_ASSERT_NOT_REACHED();
    return CCallHelpers::Address(CCallHelpers::framePointerRegister);
}

void reifyInlinedCallFrames(CCallHelpers& jit, const OSRExitBase& exit)
{
    // FIXME: We shouldn't leave holes on the stack when performing an OSR exit
    // in presence of inlined tail calls.
    // https://bugs.webkit.org/show_bug.cgi?id=147511
    ASSERT(JITCode::isBaselineCode(jit.baselineCodeBlock()->jitType()));
    jit.storePtr(AssemblyHelpers::TrustedImmPtr(jit.baselineCodeBlock()), AssemblyHelpers::addressFor(CallFrameSlot::codeBlock));

    GPRReg returnPCReg = GPRInfo::regT5;
#if CPU(ARM64E)
    GPRReg signingTagReg = GPRInfo::regT2;
    if (!Options::allowNonSPTagging()) {
        returnPCReg = ARM64Registers::lr;
        signingTagReg = MacroAssembler::stackPointerRegister;
        // We could save/restore lr here but we don't need to because the LLInt/Baseline will load it from the stack before returning anyway.
    }
#endif

    const CodeOrigin* codeOrigin;
    for (codeOrigin = &exit.m_codeOrigin; codeOrigin && codeOrigin->inlineCallFrame(); codeOrigin = codeOrigin->inlineCallFrame()->getCallerSkippingTailCalls()) {
        InlineCallFrame* inlineCallFrame = codeOrigin->inlineCallFrame();
        CodeBlock* baselineCodeBlock = jit.baselineCodeBlockFor(*codeOrigin);
        InlineCallFrame::Kind trueCallerCallKind;
        CodeOrigin* trueCaller = inlineCallFrame->getCallerSkippingTailCalls(&trueCallerCallKind);
        GPRReg callerFrameGPR = GPRInfo::callFrameRegister;

        bool callerIsLLInt = false;

        if (!trueCaller) {
            ASSERT(inlineCallFrame->isTail());
            jit.loadPtr(AssemblyHelpers::Address(GPRInfo::callFrameRegister, CallFrame::returnPCOffset()), returnPCReg);
#if CPU(ARM64E)
            if (!Options::allowNonSPTagging()) {
                JIT_COMMENT(jit, "lldb dynamic execution / posix signals could trash your stack"); // We don't have to worry about signals because they shouldn't fire in WebContent process in this window.
                jit.move(MacroAssembler::stackPointerRegister, GPRInfo::regT4);
            }

            jit.addPtr(AssemblyHelpers::TrustedImm32(sizeof(CallerFrameAndPC)), GPRInfo::callFrameRegister, GPRInfo::regT2);
            jit.untagPtr(GPRInfo::regT2, returnPCReg);
            jit.validateUntaggedPtr(returnPCReg, GPRInfo::regT2);
            jit.addPtr(AssemblyHelpers::TrustedImm32(inlineCallFrame->returnPCOffset() + sizeof(CPURegister)), GPRInfo::callFrameRegister, signingTagReg);
            jit.tagPtr(signingTagReg, returnPCReg);

            if (!Options::allowNonSPTagging()) {
                JIT_COMMENT(jit, "lldb dynamic execution / posix signals are ok again");
                jit.move(GPRInfo::regT4, MacroAssembler::stackPointerRegister);
            }
#endif
            jit.storePtr(returnPCReg, AssemblyHelpers::addressForByteOffset(inlineCallFrame->returnPCOffset()));
            jit.loadPtr(AssemblyHelpers::Address(GPRInfo::callFrameRegister, CallFrame::callerFrameOffset()), GPRInfo::regT3);
            callerFrameGPR = GPRInfo::regT3;
        } else {
            CodeBlock* baselineCodeBlockForCaller = jit.baselineCodeBlockFor(*trueCaller);
            auto callBytecodeIndex = trueCaller->bytecodeIndex();
            CodePtr<JSEntryPtrTag> jumpTarget = callerReturnPC(baselineCodeBlockForCaller, callBytecodeIndex, trueCallerCallKind, callerIsLLInt);

            if (trueCaller->inlineCallFrame()) {
                jit.addPtr(
                    AssemblyHelpers::TrustedImm32(trueCaller->inlineCallFrame()->stackOffset * sizeof(EncodedJSValue)),
                    GPRInfo::callFrameRegister,
                    GPRInfo::regT3);
                callerFrameGPR = GPRInfo::regT3;
            }

#if CPU(ARM64E)
            if (!Options::allowNonSPTagging()) {
                JIT_COMMENT(jit, "lldb dynamic execution / posix signals could trash your stack"); // We don't have to worry about signals because they shouldn't fire in WebContent process in this window.
                jit.move(MacroAssembler::stackPointerRegister, GPRInfo::regT4);
            }

            jit.addPtr(AssemblyHelpers::TrustedImm32(inlineCallFrame->returnPCOffset() + sizeof(CPURegister)), GPRInfo::callFrameRegister, signingTagReg);
            jit.move(AssemblyHelpers::TrustedImmPtr(jumpTarget.untaggedPtr()), returnPCReg);
            jit.tagPtr(signingTagReg, returnPCReg);
            jit.storePtr(returnPCReg, AssemblyHelpers::addressForByteOffset(inlineCallFrame->returnPCOffset()));

            if (!Options::allowNonSPTagging()) {
                JIT_COMMENT(jit, "lldb dynamic execution / posix signals are ok again");
                jit.move(GPRInfo::regT4, MacroAssembler::stackPointerRegister);
            }
#else
            jit.storePtr(AssemblyHelpers::TrustedImmPtr(jumpTarget.untaggedPtr()), AssemblyHelpers::addressForByteOffset(inlineCallFrame->returnPCOffset()));
#endif
        }

        jit.storePtr(AssemblyHelpers::TrustedImmPtr(baselineCodeBlock), AssemblyHelpers::addressFor((VirtualRegister)(inlineCallFrame->stackOffset + CallFrameSlot::codeBlock)));

        // Restore the inline call frame's callee save registers.
        // If this inlined frame is a tail call that will return back to the original caller, we need to
        // copy the prior contents of the tag registers already saved for the outer frame to this frame.
        jit.emitSaveOrCopyLLIntBaselineCalleeSavesFor(
            baselineCodeBlock,
            static_cast<VirtualRegister>(inlineCallFrame->stackOffset),
            trueCaller ? AssemblyHelpers::UseExistingTagRegisterContents : AssemblyHelpers::CopyBaselineCalleeSavedRegistersFromBaseFrame,
            GPRInfo::regT2, GPRInfo::regT1, GPRInfo::regT4);

        if (callerIsLLInt) {
            CodeBlock* baselineCodeBlockForCaller = jit.baselineCodeBlockFor(*trueCaller);
            jit.storePtr(CCallHelpers::TrustedImmPtr(baselineCodeBlockForCaller->metadataTable()), calleeSaveSlot(inlineCallFrame, baselineCodeBlock, LLInt::Registers::metadataTableGPR));
            jit.storePtr(CCallHelpers::TrustedImmPtr(baselineCodeBlockForCaller->instructionsRawPointer()), calleeSaveSlot(inlineCallFrame, baselineCodeBlock, LLInt::Registers::pbGPR));
        } else if (trueCaller) {
            CodeBlock* baselineCodeBlockForCaller = jit.baselineCodeBlockFor(*trueCaller);
            jit.storePtr(CCallHelpers::TrustedImmPtr(baselineCodeBlockForCaller->metadataTable()), calleeSaveSlot(inlineCallFrame, baselineCodeBlock, GPRInfo::metadataTableRegister));
            jit.storePtr(CCallHelpers::TrustedImmPtr(baselineCodeBlockForCaller->baselineJITData()), calleeSaveSlot(inlineCallFrame, baselineCodeBlock, GPRInfo::jitDataRegister));
        }

        if (!inlineCallFrame->isVarargs())
            jit.store32(AssemblyHelpers::TrustedImm32(inlineCallFrame->argumentCountIncludingThis), AssemblyHelpers::payloadFor(VirtualRegister(inlineCallFrame->stackOffset + CallFrameSlot::argumentCountIncludingThis)));
        jit.storePtr(callerFrameGPR, AssemblyHelpers::addressForByteOffset(inlineCallFrame->callerFrameOffset()));

        BytecodeIndex exitIndex = baselineCodeBlock->bytecodeIndexForExit(codeOrigin->bytecodeIndex());
        uint32_t locationBits = CallSiteIndex(exitIndex).bits();
        jit.store32(AssemblyHelpers::TrustedImm32(locationBits), AssemblyHelpers::tagFor(VirtualRegister(inlineCallFrame->stackOffset + CallFrameSlot::argumentCountIncludingThis)));
        if (!inlineCallFrame->isClosureCall)
            jit.storeCell(AssemblyHelpers::TrustedImmPtr(inlineCallFrame->calleeConstant()), AssemblyHelpers::addressFor(VirtualRegister(inlineCallFrame->stackOffset + CallFrameSlot::callee)));
    }

    // Don't need to set the toplevel code origin if we only did inline tail calls
    if (codeOrigin) {
        BytecodeIndex exitIndex(codeOrigin->bytecodeIndex().offset());
        uint32_t locationBits = CallSiteIndex(exitIndex).bits();
        jit.store32(AssemblyHelpers::TrustedImm32(locationBits), AssemblyHelpers::tagFor(CallFrameSlot::argumentCountIncludingThis));
    }
}

static void osrWriteBarrier(VM& vm, CCallHelpers& jit, GPRReg owner, GPRReg scratch)
{
    AssemblyHelpers::Jump ownerIsRememberedOrInEden = jit.barrierBranchWithoutFence(owner);

    jit.setupArguments<decltype(operationOSRWriteBarrier)>(CCallHelpers::TrustedImmPtr(&vm), owner);
    jit.prepareCallOperation(vm);
    jit.move(MacroAssembler::TrustedImmPtr(tagCFunction<OperationPtrTag>(operationOSRWriteBarrier)), scratch);
    jit.call(scratch, OperationPtrTag);

    ownerIsRememberedOrInEden.link(&jit);
}

void adjustAndJumpToTarget(VM& vm, CCallHelpers& jit, const OSRExitBase& exit)
{
    jit.memoryFence();
    
    jit.move(
        AssemblyHelpers::TrustedImmPtr(
            jit.codeBlock()->baselineAlternative()), GPRInfo::argumentGPR1);
    osrWriteBarrier(vm, jit, GPRInfo::argumentGPR1, GPRInfo::nonArgGPR0);

    // We barrier all inlined frames -- and not just the current inline stack --
    // because we don't know which inlined function owns the value profile that
    // we'll update when we exit. In the case of "f() { a(); b(); }", if both
    // a and b are inlined, we might exit inside b due to a bad value loaded
    // from a.
    // FIXME: MethodOfGettingAValueProfile should remember which CodeBlock owns
    // the value profile.
    InlineCallFrameSet* inlineCallFrames = jit.codeBlock()->jitCode()->dfgCommon()->inlineCallFrames.get();
    if (inlineCallFrames) {
        for (InlineCallFrame* inlineCallFrame : *inlineCallFrames) {
            jit.move(
                AssemblyHelpers::TrustedImmPtr(
                    inlineCallFrame->baselineCodeBlock.get()), GPRInfo::argumentGPR1);
            osrWriteBarrier(vm, jit, GPRInfo::argumentGPR1, GPRInfo::nonArgGPR0);
        }
    }

    auto* exitInlineCallFrame = exit.m_codeOrigin.inlineCallFrame();
    if (exitInlineCallFrame)
        jit.addPtr(AssemblyHelpers::TrustedImm32(exitInlineCallFrame->stackOffset * sizeof(EncodedJSValue)), GPRInfo::callFrameRegister);

    CodeBlock* codeBlockForExit = jit.baselineCodeBlockFor(exit.m_codeOrigin);
    ASSERT(codeBlockForExit == codeBlockForExit->baselineVersion());
    ASSERT(JITCode::isBaselineCode(codeBlockForExit->jitType()));

    void* jumpTarget;
    bool exitToLLInt = Options::forceOSRExitToLLInt() || codeBlockForExit->jitType() == JITType::InterpreterThunk;
    if (exitToLLInt) {
        auto bytecodeIndex = exit.m_codeOrigin.bytecodeIndex();
        const auto& currentInstruction = *codeBlockForExit->instructions().at(bytecodeIndex).ptr();
        CodePtr<JSEntryPtrTag> destination;
        if (bytecodeIndex.checkpoint())
            destination = LLInt::checkpointOSRExitTrampolineThunk().code();
        else 
            destination = LLInt::normalOSRExitTrampolineThunk().code();

        if (exit.isExceptionHandler()) {
            jit.move(CCallHelpers::TrustedImmPtr(&currentInstruction), GPRInfo::regT2);
            jit.storePtr(GPRInfo::regT2, &std::get<const JSInstruction*>(vm.targetInterpreterPCForThrow));
        }

        jit.move(CCallHelpers::TrustedImmPtr(codeBlockForExit->metadataTable()), LLInt::Registers::metadataTableGPR);
        jit.move(CCallHelpers::TrustedImmPtr(codeBlockForExit->instructionsRawPointer()), LLInt::Registers::pbGPR);
        jit.move(CCallHelpers::TrustedImm32(bytecodeIndex.offset()), LLInt::Registers::pcGPR);
        jumpTarget = destination.retagged<OSRExitPtrTag>().taggedPtr();
    } else {
        jit.move(CCallHelpers::TrustedImmPtr(codeBlockForExit->metadataTable()), GPRInfo::metadataTableRegister);
        jit.move(CCallHelpers::TrustedImmPtr(codeBlockForExit->baselineJITData()), GPRInfo::jitDataRegister);

        BytecodeIndex exitIndex = exit.m_codeOrigin.bytecodeIndex();
        CodePtr<JSEntryPtrTag> destination;
        if (exitIndex.checkpoint())
            destination = LLInt::checkpointOSRExitTrampolineThunk().code();
        else {
            ASSERT(codeBlockForExit->bytecodeIndexForExit(exitIndex) == exitIndex);
            destination = codeBlockForExit->jitCodeMap().find(exitIndex);
        }

        ASSERT(destination);

        jumpTarget = destination.retagged<OSRExitPtrTag>().taggedPtr();
    }

    if (exit.isExceptionHandler()) {
        ASSERT(!RegisterSetBuilder::vmCalleeSaveRegisters().contains(LLInt::Registers::pcGPR, IgnoreVectors));
        jit.copyCalleeSavesToEntryFrameCalleeSavesBuffer(vm.topEntryFrame, AssemblyHelpers::selectScratchGPR(LLInt::Registers::pcGPR));

        // Since we're jumping to op_catch, we need to set callFrameForCatch.
        jit.storePtr(GPRInfo::callFrameRegister, vm.addressOfCallFrameForCatch());
    }

    jit.addPtr(AssemblyHelpers::TrustedImm32(JIT::stackPointerOffsetFor(codeBlockForExit) * sizeof(Register)), GPRInfo::callFrameRegister, AssemblyHelpers::stackPointerRegister);

    jit.move(AssemblyHelpers::TrustedImmPtr(jumpTarget), GPRInfo::regT2);
    jit.farJump(GPRInfo::regT2, OSRExitPtrTag);
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)

