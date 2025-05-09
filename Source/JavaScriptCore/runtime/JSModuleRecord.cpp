/*
 * Copyright (C) 2015-2025 Apple Inc. All rights reserved.
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
#include "JSModuleRecord.h"

#include "BuiltinNames.h"
#include "Interpreter.h"
#include "JSAsyncFunction.h"
#include "JSAsyncGeneratorFunction.h"
#include "JSCInlines.h"
#include "JSGeneratorFunction.h"
#include "JSModuleEnvironment.h"
#include "JSModuleLoader.h"
#include "JSModuleNamespaceObject.h"
#include "SourceProfiler.h"
#include "UnlinkedModuleProgramCodeBlock.h"
#include <wtf/text/MakeString.h>

namespace JSC {

const ClassInfo JSModuleRecord::s_info = { "ModuleRecord"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSModuleRecord) };


Structure* JSModuleRecord::createStructure(VM& vm, JSGlobalObject* globalObject, JSValue prototype)
{
    return Structure::create(vm, globalObject, prototype, TypeInfo(ObjectType, StructureFlags), info());
}

JSModuleRecord* JSModuleRecord::create(JSGlobalObject* globalObject, VM& vm, Structure* structure, const Identifier& moduleKey, const SourceCode& sourceCode, const VariableEnvironment& declaredVariables, const VariableEnvironment& lexicalVariables, CodeFeatures features)
{
    JSModuleRecord* instance = new (NotNull, allocateCell<JSModuleRecord>(vm)) JSModuleRecord(vm, structure, moduleKey, sourceCode, declaredVariables, lexicalVariables, features);
    instance->finishCreation(globalObject, vm);
    return instance;
}
JSModuleRecord::JSModuleRecord(VM& vm, Structure* structure, const Identifier& moduleKey, const SourceCode& sourceCode, const VariableEnvironment& declaredVariables, const VariableEnvironment& lexicalVariables, CodeFeatures features)
    : Base(vm, structure, moduleKey)
    , m_sourceCode(sourceCode)
    , m_declaredVariables(declaredVariables)
    , m_lexicalVariables(lexicalVariables)
    , m_features(features)
{
}

void JSModuleRecord::destroy(JSCell* cell)
{
    JSModuleRecord* thisObject = static_cast<JSModuleRecord*>(cell);
    thisObject->JSModuleRecord::~JSModuleRecord();
}

void JSModuleRecord::finishCreation(JSGlobalObject* globalObject, VM& vm)
{
    Base::finishCreation(globalObject, vm);
    ASSERT(inherits(info()));
}

template<typename Visitor>
void JSModuleRecord::visitChildrenImpl(JSCell* cell, Visitor& visitor)
{
    JSModuleRecord* thisObject = jsCast<JSModuleRecord*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    Base::visitChildren(thisObject, visitor);
    visitor.append(thisObject->m_moduleProgramExecutable);
}

DEFINE_VISIT_CHILDREN(JSModuleRecord);

Synchronousness JSModuleRecord::link(JSGlobalObject* globalObject, JSValue scriptFetcher)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (SourceProfiler::g_profilerHook) [[unlikely]]
        SourceProfiler::profile(SourceProfiler::Type::Module, sourceCode());

    ModuleProgramExecutable* executable = ModuleProgramExecutable::tryCreate(globalObject, sourceCode());
    RETURN_IF_EXCEPTION(scope, Synchronousness::Sync);

    instantiateDeclarations(globalObject, executable, scriptFetcher);
    RETURN_IF_EXCEPTION(scope, Synchronousness::Sync);
    m_moduleProgramExecutable.set(vm, this, executable);

    return executable->unlinkedCodeBlock()->isAsync() ? Synchronousness::Async : Synchronousness::Sync;
}

void JSModuleRecord::instantiateDeclarations(JSGlobalObject* globalObject, ModuleProgramExecutable* moduleProgramExecutable, JSValue scriptFetcher)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    // http://www.ecma-international.org/ecma-262/6.0/#sec-moduledeclarationinstantiation

    SymbolTable* symbolTable = moduleProgramExecutable->moduleEnvironmentSymbolTable();
    JSModuleEnvironment* moduleEnvironment = JSModuleEnvironment::create(vm, globalObject, globalObject->globalLexicalEnvironment(), symbolTable, jsTDZValue(), this);

    // http://www.ecma-international.org/ecma-262/6.0/#sec-moduledeclarationinstantiation
    // section 15.2.1.16.4 step 9.
    // Ensure all the indirect exports are correctly resolved to unique bindings.
    // Even if we avoided duplicate exports in the parser, still ambiguous exports occur due to the star export (`export * from "mod"`).
    // When we see this type of ambiguity for the indirect exports here, throw a syntax error.
    for (const auto& pair : exportEntries()) {
        const ExportEntry& exportEntry = pair.value;
        switch (exportEntry.type) {
        case ExportEntry::Type::Local:
        case ExportEntry::Type::Namespace:
            break;
        case ExportEntry::Type::Indirect: {
            Resolution resolution = resolveExport(globalObject, exportEntry.exportName);
            RETURN_IF_EXCEPTION(scope, void());
            switch (resolution.type) {
            case Resolution::Type::NotFound:
                throwSyntaxError(globalObject, scope, makeString("Indirectly exported binding name '"_s, StringView(exportEntry.exportName.impl()), "' is not found."_s));
                return;

            case Resolution::Type::Ambiguous:
                throwSyntaxError(globalObject, scope, makeString("Indirectly exported binding name '"_s, StringView(exportEntry.exportName.impl()), "' cannot be resolved due to ambiguous multiple bindings."_s));
                return;

            case Resolution::Type::Error:
                throwSyntaxError(globalObject, scope, "Indirectly exported binding name 'default' cannot be resolved by star export entries."_s);
                return;

            case Resolution::Type::Resolved:
                break;
            }
            break;
        }
        }
    }

    // https://tc39.es/ecma262/#sec-source-text-module-record-initialize-environment step 8
    // Instantiate namespace objects and initialize the bindings with them if required.
    // And ensure that all the imports correctly resolved to unique bindings.
    for (const auto& pair : importEntries()) {
        const ImportEntry& importEntry = pair.value;
        AbstractModuleRecord* importedModule = hostResolveImportedModule(globalObject, importEntry.moduleRequest);

#if CPU(ADDRESS64)
        // rdar://107531050: Speculative crash mitigation
        if (importedModule == std::bit_cast<AbstractModuleRecord*>(encodedJSUndefined())) [[unlikely]] {
            RELEASE_ASSERT(vm.exceptionForInspection(), vm.traps().maybeNeedHandling(), vm.exceptionForInspection(), importedModule);
            RELEASE_ASSERT(vm.traps().maybeNeedHandling(), vm.traps().maybeNeedHandling(), vm.exceptionForInspection(), importedModule);
            if (!vm.exceptionForInspection() || !vm.traps().maybeNeedHandling()) {
                throwSyntaxError(globalObject, scope, makeString("Importing module '"_s, String(importEntry.moduleRequest.impl()), "' is not found."_s));
                return;
            }
        }
#endif

        RETURN_IF_EXCEPTION(scope, void());
        switch (importEntry.type) {
        case AbstractModuleRecord::ImportEntryType::Namespace: {
            JSModuleNamespaceObject* namespaceObject = importedModule->getModuleNamespace(globalObject);
            RETURN_IF_EXCEPTION(scope, void());
            bool putResult = false;
            symbolTablePutTouchWatchpointSet(moduleEnvironment, globalObject, importEntry.localName, namespaceObject, /* shouldThrowReadOnlyError */ false, /* ignoreReadOnlyErrors */ true, putResult);
            RETURN_IF_EXCEPTION(scope, void());
            break;
        }

        case AbstractModuleRecord::ImportEntryType::Single: {
            Resolution resolution = importedModule->resolveExport(globalObject, importEntry.importName);
            RETURN_IF_EXCEPTION(scope, void());
            switch (resolution.type) {
            case Resolution::Type::NotFound:
                throwSyntaxError(globalObject, scope, makeString("Importing binding name '"_s, StringView(importEntry.importName.impl()), "' is not found."_s));
                return;

            case Resolution::Type::Ambiguous:
                throwSyntaxError(globalObject, scope, makeString("Importing binding name '"_s, StringView(importEntry.importName.impl()), "' cannot be resolved due to ambiguous multiple bindings."_s));
                return;

            case Resolution::Type::Error:
                throwSyntaxError(globalObject, scope, "Importing binding name 'default' cannot be resolved by star export entries."_s);
                return;

            case Resolution::Type::Resolved: {
                if (vm.propertyNames->starNamespacePrivateName == resolution.localName) {
                    resolution.moduleRecord->getModuleNamespace(globalObject); // Force module namespace object materialization.
                    RETURN_IF_EXCEPTION(scope, void());
                }
                break;
            }
            }
            break;
        }
        }
    }

    // http://www.ecma-international.org/ecma-262/6.0/#sec-moduledeclarationinstantiation
    // section 15.2.1.16.4 step 14.
    // Module environment contains the heap allocated "var", "function", "let", "const", and "class".
    // When creating the environment, we initialized all the slots with empty, it's ok for lexical values.
    // But for "var" and "function", we should initialize it with undefined. They are contained in the declared variables.
    for (const auto& variable : declaredVariables()) {
        SymbolTableEntry entry = symbolTable->get(variable.key.get());
        VarOffset offset = entry.varOffset();
        if (!offset.isStack()) {
            bool putResult = false;
            symbolTablePutTouchWatchpointSet(moduleEnvironment, globalObject, Identifier::fromUid(vm, variable.key.get()), jsUndefined(), /* shouldThrowReadOnlyError */ false, /* ignoreReadOnlyErrors */ true, putResult);
            RETURN_IF_EXCEPTION(scope, void());
        }
    }

    // http://www.ecma-international.org/ecma-262/6.0/#sec-moduledeclarationinstantiation
    // section 15.2.1.16.4 step 16-a-iv.
    // Initialize heap allocated function declarations.
    // They can be called before the body of the module is executed under circular dependencies.
    UnlinkedModuleProgramCodeBlock* unlinkedCodeBlock = moduleProgramExecutable->unlinkedCodeBlock();
    for (size_t i = 0, numberOfFunctions = unlinkedCodeBlock->numberOfFunctionDecls(); i < numberOfFunctions; ++i) {
        UnlinkedFunctionExecutable* unlinkedFunctionExecutable = unlinkedCodeBlock->functionDecl(i);
        SymbolTableEntry entry = symbolTable->get(unlinkedFunctionExecutable->name().impl());
        VarOffset offset = entry.varOffset();
        if (!offset.isStack()) {
            ASSERT(!unlinkedFunctionExecutable->name().isEmpty());
            if (vm.typeProfiler() || vm.controlFlowProfiler()) {
                vm.functionHasExecutedCache()->insertUnexecutedRange(moduleProgramExecutable->sourceID(),
                    unlinkedFunctionExecutable->unlinkedFunctionStart(),
                    unlinkedFunctionExecutable->unlinkedFunctionEnd());
            }
            auto* executable = unlinkedFunctionExecutable->link(vm, moduleProgramExecutable, moduleProgramExecutable->source());
            SourceParseMode parseMode = executable->parseMode();
            JSFunction* function = nullptr;
            if (isAsyncGeneratorWrapperParseMode(parseMode))
                function = JSAsyncGeneratorFunction::create(vm, globalObject, executable, moduleEnvironment);
            else if (isGeneratorWrapperParseMode(parseMode))
                function = JSGeneratorFunction::create(vm, globalObject, executable, moduleEnvironment);
            else if (isAsyncFunctionWrapperParseMode(parseMode))
                function = JSAsyncFunction::create(vm, globalObject, executable, moduleEnvironment);
            else
                function = JSFunction::create(vm, globalObject, executable, moduleEnvironment);
            bool putResult = false;
            symbolTablePutTouchWatchpointSet(moduleEnvironment, globalObject, unlinkedFunctionExecutable->name(), function, /* shouldThrowReadOnlyError */ false, /* ignoreReadOnlyErrors */ true, putResult);
            RETURN_IF_EXCEPTION(scope, void());
        }
    }

    if (m_features & ImportMetaFeature) {
        JSObject* metaProperties = globalObject->moduleLoader()->createImportMetaProperties(globalObject, identifierToJSValue(vm, moduleKey()), this, scriptFetcher);
        RETURN_IF_EXCEPTION(scope, void());
        bool putResult = false;
        symbolTablePutTouchWatchpointSet(moduleEnvironment, globalObject, vm.propertyNames->builtinNames().metaPrivateName(), metaProperties, /* shouldThrowReadOnlyError */ false, /* ignoreReadOnlyErrors */ true, putResult);
        RETURN_IF_EXCEPTION(scope, void());
    }

    scope.release();
    setModuleEnvironment(globalObject, moduleEnvironment);
}

JSValue JSModuleRecord::evaluate(JSGlobalObject* globalObject, JSValue sentValue, JSValue resumeMode)
{
    if (!m_moduleProgramExecutable)
        return jsUndefined();
    VM& vm = globalObject->vm();
    ModuleProgramExecutable* executable = m_moduleProgramExecutable.get();
    JSValue resultOrAwaitedValue = vm.interpreter.executeModuleProgram(this, executable, globalObject, moduleEnvironment(), sentValue, resumeMode);
    if (JSValue state = internalField(Field::State).get(); !state.isNumber() || state.asNumber() == static_cast<unsigned>(State::Executing))
        m_moduleProgramExecutable.clear();
    return resultOrAwaitedValue;
}

} // namespace JSC
