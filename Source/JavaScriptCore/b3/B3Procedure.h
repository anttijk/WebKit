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

#pragma once

#if ENABLE(B3_JIT)

#include "B3Origin.h"
#include "B3PCToOriginMap.h"
#include "B3SparseCollection.h"
#include "B3Type.h"
#include "B3ValueKey.h"
#include "JITOpaqueByproducts.h"
#include "PureNaN.h"
#include "RegisterAtOffsetList.h"
#include <wtf/Bag.h>
#include <wtf/FastMalloc.h>
#include <wtf/HashSet.h>
#include <wtf/IndexedContainerIterator.h>
#include <wtf/Noncopyable.h>
#include <wtf/PrintStream.h>
#include <wtf/SequesteredMalloc.h>
#include <wtf/SharedTask.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/TriState.h>
#include <wtf/Vector.h>

namespace JSC {

class CCallHelpers;

namespace B3 {

class BackwardsCFG;
class BackwardsDominators;
class BasicBlock;
class BlockInsertionSet;
class CFG;
class Dominators;
class NaturalLoops;
class Value;
class Variable;
class WasmBoundsCheckValue;

namespace Air {
class Code;
class StackSlot;
} // namespace Air

typedef void WasmBoundsCheckGeneratorFunction(CCallHelpers&, WasmBoundsCheckValue*, GPRReg);
typedef SharedTask<WasmBoundsCheckGeneratorFunction> WasmBoundsCheckGenerator;

// This represents B3's view of a piece of code. Note that this object must exist in a 1:1
// relationship with Air::Code. B3::Procedure and Air::Code are just different facades of the B3
// compiler's knowledge about a piece of code. Some kinds of state aren't perfect fits for either
// Procedure or Code, and are placed in one or the other based on convenience. Procedure always
// allocates a Code, and a Code cannot be allocated without an owning Procedure and they always
// have references to each other.

class Procedure {
    WTF_MAKE_NONCOPYABLE(Procedure);
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(Procedure);
public:

    JS_EXPORT_PRIVATE Procedure(bool usesSIMD = false);
    JS_EXPORT_PRIVATE ~Procedure();

    template<typename Callback>
    void setOriginPrinter(Callback&& callback)
    {
        m_originPrinter = createSharedTask<void(PrintStream&, Origin)>(
            std::forward<Callback>(callback));
    }

    // Usually you use this via OriginDump, though it's cool to use it directly.
    void printOrigin(PrintStream& out, Origin origin) const;

    // This is a debugging hack. Sometimes while debugging B3 you need to break the abstraction
    // and get at the DFG Graph, or whatever data structure the frontend used to describe the
    // program. The FTL passes the DFG Graph.
    void setFrontendData(const void* value) { m_frontendData = value; }
    const void* frontendData() const { return m_frontendData; }

    JS_EXPORT_PRIVATE BasicBlock* addBlock(double frequency = 1);

    // Changes the order of basic blocks to be as in the supplied vector. The vector does not
    // need to mention every block in the procedure. Blocks not mentioned will be placed after
    // these blocks in the same order as they were in originally.
    template<typename BlockIterable>
    void setBlockOrder(const BlockIterable& iterable)
    {
        Vector<BasicBlock*> blocks;
        for (BasicBlock* block : iterable)
            blocks.append(block);
        setBlockOrderImpl(blocks);
    }

    JS_EXPORT_PRIVATE Air::StackSlot* addStackSlot(uint64_t byteSize);
    JS_EXPORT_PRIVATE Variable* addVariable(Type);

    JS_EXPORT_PRIVATE Type addTuple(Vector<Type>&& types);
    const Vector<Vector<Type>>& tuples() const { return m_tuples; };
    bool isValidTuple(Type tuple) const;
    Type extractFromTuple(Type tuple, unsigned index) const;
    JS_EXPORT_PRIVATE const Vector<Type>& tupleForType(Type tuple) const;

    unsigned resultCount(Type type) const { return type.isTuple() ? tupleForType(type).size() : type.isNumeric(); }
    Type typeAtOffset(Type type, unsigned index) const { ASSERT(index < resultCount(type)); return type.isTuple() ? extractFromTuple(type, index) : type; }

    template<typename ValueType, typename... Arguments>
    ValueType* add(Arguments...);

    Value* clone(Value*);

    Value* addIntConstant(Origin, Type, int64_t value);
    Value* addIntConstant(Value*, int64_t value);

    // bits is a std::bit_cast of the constant you want.
    JS_EXPORT_PRIVATE Value* addConstant(Origin, Type, uint64_t bits);
    JS_EXPORT_PRIVATE Value* addConstant(Origin, Type, v128_t bits);

    // You're guaranteed that bottom is zero.
    Value* addBottom(Origin, Type);
    Value* addBottom(Value*);

    // Returns null for TriState::Indeterminate.
    Value* addBoolConstant(Origin, TriState);

    void resetValueOwners();
    JS_EXPORT_PRIVATE void resetReachability();

    // This destroys CFG analyses. If we ask for them again, we will recompute them. Usually you
    // should call this anytime you call resetReachability().
    void invalidateCFG();

    JS_EXPORT_PRIVATE void dump(PrintStream&) const;

    unsigned size() const { return m_blocks.size(); }
    BasicBlock* at(unsigned index) const { return m_blocks[index].get(); }
    BasicBlock* operator[](unsigned index) const { return at(index); }

    typedef WTF::IndexedContainerIterator<Procedure> iterator;

    iterator begin() const { return iterator(*this, 0); }
    iterator end() const { return iterator(*this, size()); }

    Vector<BasicBlock*> blocksInPreOrder();
    Vector<BasicBlock*> blocksInPostOrder();

    SparseCollection<Air::StackSlot>& stackSlots();
    const SparseCollection<Air::StackSlot>& stackSlots() const;

    SparseCollection<Variable>& variables() { return m_variables; }
    const SparseCollection<Variable>& variables() const { return m_variables; }

    // Short for variables().remove(). It's better to call this method since it's out of line.
    void deleteVariable(Variable*);

    SparseCollection<Value>& values() { return m_values; }
    const SparseCollection<Value>& values() const { return m_values; }

    // Short for values().remove(). It's better to call this method since it's out of line.
    void deleteValue(Value*);

    // A valid procedure cannot contain any orphan values. An orphan is a value that is not in
    // any basic block. It is possible to create an orphan value during code generation or during
    // transformation. If you know that you may have created some, you can call this method to
    // delete them, making the procedure valid again.
    void deleteOrphans();

    CFG& cfg() const { return *m_cfg; }

    Dominators& dominators();
    JS_EXPORT_PRIVATE NaturalLoops& naturalLoops();
    BackwardsCFG& backwardsCFG();
    BackwardsDominators& backwardsDominators();

    void addFastConstant(const ValueKey&);
    bool isFastConstant(const ValueKey&);
    
    unsigned numEntrypoints() const { return m_numEntrypoints; }
    JS_EXPORT_PRIVATE void setNumEntrypoints(unsigned);

    // The name has to be a string literal, since we don't do any memory management for the string.
    void setLastPhaseName(const char* name)
    {
        m_lastPhaseName = name;
    }

    const char* lastPhaseName() const { return m_lastPhaseName; }

    // Allocates a slab of memory that will be kept alive by anyone who keeps the resulting code
    // alive. Great for compiler-generated data sections, like switch jump tables and constant pools.
    // This returns memory that has been zero-initialized.
    JS_EXPORT_PRIVATE void* addDataSection(size_t);
    
    // Some operations are specified in B3 IR to behave one way but on this given CPU they behave a
    // different way. When true, those B3 IR ops switch to behaving the CPU way, and the optimizer may
    // start taking advantage of it.
    //
    // One way to think of it is like this. Imagine that you find that the cleanest way of lowering
    // something in lowerMacros is to unconditionally replace one opcode with another. This is a shortcut
    // where you instead keep the same opcode, but rely on the opcode's meaning changes once lowerMacros
    // sets hasQuirks.
    bool hasQuirks() const { return m_hasQuirks; }
    void setHasQuirks(bool value) { m_hasQuirks = value; }

    OpaqueByproducts& byproducts() { return *m_byproducts; }

    // Below are methods that make sense to call after you have generated code for the procedure.

    // You have to call this method after calling generate(). The code generated by B3::generate()
    // will require you to keep this object alive for as long as that code is runnable. Usually, this
    // just keeps alive things like the double constant pool and switch lookup tables. If this sounds
    // confusing, you should probably be using the JSC::Compilation API to compile code. If you use
    // that API, then you don't have to worry about this.
    std::unique_ptr<OpaqueByproducts> releaseByproducts() { return WTFMove(m_byproducts); }

    // This gives you direct access to Code. However, the idea is that clients of B3 shouldn't have to
    // call this. So, Procedure has some methods (below) that expose some Air::Code functionality.
    const Air::Code& code() const { return *m_code; }
    Air::Code& code() { return *m_code; }

    unsigned callArgAreaSizeInBytes() const;
    void requestCallArgAreaSizeInBytes(unsigned size);

    // This tells the register allocators to stay away from this register.
    JS_EXPORT_PRIVATE void pinRegister(Reg);
    
    JS_EXPORT_PRIVATE void setOptLevel(unsigned value);
    unsigned optLevel() const { return m_optLevel; }
    
    // You can turn off used registers calculation. This may speed up compilation a bit. But if
    // you turn it off then you cannot use StackmapGenerationParams::usedRegisters() or
    // StackmapGenerationParams::unavailableRegisters().
    void setNeedsUsedRegisters(bool value) { m_needsUsedRegisters = value; }
    bool needsUsedRegisters() const { return m_needsUsedRegisters; }

    JS_EXPORT_PRIVATE unsigned frameSize() const;
    JS_EXPORT_PRIVATE RegisterAtOffsetList calleeSaveRegisterAtOffsetList() const;

    PCToOriginMap& pcToOriginMap() { return m_pcToOriginMap; }
    PCToOriginMap releasePCToOriginMap()
    {
        RELEASE_ASSERT(needsPCToOriginMap());
        return WTFMove(m_pcToOriginMap);
    }

    JS_EXPORT_PRIVATE void setWasmBoundsCheckGenerator(RefPtr<WasmBoundsCheckGenerator>);

    template<typename Functor>
    void setWasmBoundsCheckGenerator(const Functor& functor)
    {
        setWasmBoundsCheckGenerator(RefPtr<WasmBoundsCheckGenerator>(createSharedTask<WasmBoundsCheckGeneratorFunction>(functor)));
    }

    JS_EXPORT_PRIVATE RegisterSetBuilder mutableGPRs();

    void setNeedsPCToOriginMap();
    bool needsPCToOriginMap() { return m_needsPCToOriginMap; }

    JS_EXPORT_PRIVATE void freeUnneededB3ValuesAfterLowering();

    bool shouldDumpIR() const { return m_shouldDumpIR; }
    JS_EXPORT_PRIVATE void setShouldDumpIR();

    void setUsessSIMD()
    { 
        RELEASE_ASSERT(Options::useWasmSIMD());
        m_usesSIMD = true;
    }
    bool usesSIMD() const
    {
        // See also: WasmModuleInformation::usesSIMD().
        if (!Options::useWasmSIMD())
            return false;
        if (Options::forceAllFunctionsToUseSIMD())
            return true;
        // The LLInt discovers this value.
        ASSERT(Options::useWasmLLInt() || Options::useWasmIPInt());
        return m_usesSIMD;
    }

private:
    friend class BlockInsertionSet;

    JS_EXPORT_PRIVATE Value* addValueImpl(Value*);
    void setBlockOrderImpl(Vector<BasicBlock*>&);

    SparseCollection<Variable> m_variables;
    Vector<Vector<Type>> m_tuples;
    Vector<std::unique_ptr<BasicBlock>> m_blocks;
    SparseCollection<Value> m_values;
    std::unique_ptr<CFG> m_cfg;
    std::unique_ptr<Dominators> m_dominators;
    std::unique_ptr<NaturalLoops> m_naturalLoops;
    std::unique_ptr<BackwardsCFG> m_backwardsCFG;
    std::unique_ptr<BackwardsDominators> m_backwardsDominators;
    UncheckedKeyHashSet<ValueKey> m_fastConstants;
    const char* m_lastPhaseName;
    std::unique_ptr<OpaqueByproducts> m_byproducts;
    std::unique_ptr<Air::Code> m_code;
    RefPtr<SharedTask<void(PrintStream&, Origin)>> m_originPrinter;
    const void* m_frontendData;
    PCToOriginMap m_pcToOriginMap;
    unsigned m_numEntrypoints { 1 };
    unsigned m_optLevel { defaultOptLevel() };
    bool m_needsUsedRegisters { true };
    bool m_hasQuirks { false };
    bool m_needsPCToOriginMap { false };
    bool m_shouldDumpIR { false };
    bool m_usesSIMD { false };
};
    
} } // namespace JSC::B3

#endif // ENABLE(B3_JIT)
