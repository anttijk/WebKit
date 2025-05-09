/*
 * Copyright (C) 2013-2021 Apple Inc. All rights reserved.
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
#include "MapConstructor.h"

#include "BuiltinNames.h"
#include "GetterSetter.h"
#include "IteratorOperations.h"
#include "JSCInlines.h"
#include "JSMapInlines.h"
#include "MapPrototype.h"
#include "VMInlines.h"

namespace JSC {

const ClassInfo MapConstructor::s_info = { "Function"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(MapConstructor) };

void MapConstructor::finishCreation(VM& vm, MapPrototype* mapPrototype)
{
    Base::finishCreation(vm, 0, "Map"_s, PropertyAdditionMode::WithoutStructureTransition);
    putDirectWithoutTransition(vm, vm.propertyNames->prototype, mapPrototype, PropertyAttribute::DontEnum | PropertyAttribute::DontDelete | PropertyAttribute::ReadOnly);

    JSGlobalObject* globalObject = mapPrototype->globalObject();

    GetterSetter* speciesGetterSetter = GetterSetter::create(vm, globalObject, JSFunction::create(vm, globalObject, 0, "get [Symbol.species]"_s, globalFuncSpeciesGetter, ImplementationVisibility::Public, SpeciesGetterIntrinsic), nullptr);
    putDirectNonIndexAccessorWithoutTransition(vm, vm.propertyNames->speciesSymbol, speciesGetterSetter, PropertyAttribute::Accessor | PropertyAttribute::ReadOnly | PropertyAttribute::DontEnum);
    JSC_BUILTIN_FUNCTION_WITHOUT_TRANSITION(vm.propertyNames->builtinNames().groupByPublicName(), mapConstructorGroupByCodeGenerator, static_cast<unsigned>(PropertyAttribute::DontEnum));
}

static JSC_DECLARE_HOST_FUNCTION(callMap);
static JSC_DECLARE_HOST_FUNCTION(constructMap);

MapConstructor::MapConstructor(VM& vm, Structure* structure)
    : Base(vm, structure, callMap, constructMap)
{
}

JSC_DEFINE_HOST_FUNCTION(callMap, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    return JSValue::encode(throwConstructorCannotBeCalledAsFunctionTypeError(globalObject, scope, "Map"_s));
}

JSC_DEFINE_HOST_FUNCTION(constructMap, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSObject* newTarget = asObject(callFrame->newTarget());
    Structure* mapStructure = JSC_GET_DERIVED_STRUCTURE(vm, mapStructure, newTarget, callFrame->jsCallee());
    RETURN_IF_EXCEPTION(scope, { });

    JSValue iterable = callFrame->argument(0);
    if (iterable.isUndefinedOrNull())
        return JSValue::encode(JSMap::create(vm, mapStructure));

    bool canPerformFastSet = JSMap::isSetFastAndNonObservable(mapStructure);
    if (auto* iterableMap = jsDynamicCast<JSMap*>(iterable)) {
        if (canPerformFastSet && iterableMap->isIteratorProtocolFastAndNonObservable())
            RELEASE_AND_RETURN(scope, JSValue::encode(iterableMap->clone(globalObject, vm, mapStructure)));
    }

    JSMap* map = JSMap::create(vm, mapStructure);

    JSValue adderFunction;
    CallData adderFunctionCallData;
    if (!canPerformFastSet) {
        adderFunction = map->JSObject::get(globalObject, vm.propertyNames->set);
        RETURN_IF_EXCEPTION(scope, { });

        adderFunctionCallData = JSC::getCallData(adderFunction);
        if (adderFunctionCallData.type == CallData::Type::None) [[unlikely]]
            return throwVMTypeError(globalObject, scope, "'set' property of a Map should be callable."_s);
    }

    scope.release();
    forEachInIterable(globalObject, iterable, [&](VM& vm, JSGlobalObject* globalObject, JSValue nextItem) {
        auto scope = DECLARE_THROW_SCOPE(vm);
        if (!nextItem.isObject()) {
            throwTypeError(globalObject, scope);
            return;
        }

        JSObject* nextObject = asObject(nextItem);

        JSValue key = nextObject->getIndex(globalObject, static_cast<unsigned>(0));
        RETURN_IF_EXCEPTION(scope, void());

        JSValue value = nextObject->getIndex(globalObject, static_cast<unsigned>(1));
        RETURN_IF_EXCEPTION(scope, void());

        scope.release();
        if (canPerformFastSet) {
            map->set(mapStructure->globalObject(), key, value);
            return;
        }

        MarkedArgumentBuffer arguments;
        arguments.append(key);
        arguments.append(value);
        ASSERT(!arguments.hasOverflowed());
        call(globalObject, adderFunction, adderFunctionCallData, map, arguments);
    });

    return JSValue::encode(map);
}

JSC_DEFINE_HOST_FUNCTION(mapPrivateFuncMapIterationNext, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    ASSERT(callFrame->argument(0).isCell() && (callFrame->argument(1).isInt32()));

    VM& vm = globalObject->vm();
    JSCell* cell = callFrame->uncheckedArgument(0).asCell();
    if (cell == vm.orderedHashTableSentinel())
        return JSValue::encode(vm.orderedHashTableSentinel());

    JSMap::Storage& storage = *jsCast<JSMap::Storage*>(cell);
    JSMap::Helper::Entry entry = JSMap::Helper::toNumber(callFrame->uncheckedArgument(1));
    return JSValue::encode(JSMap::Helper::nextAndUpdateIterationEntry(vm, storage, entry));
}

JSC_DEFINE_HOST_FUNCTION(mapPrivateFuncMapIterationEntry, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    ASSERT(callFrame->argument(0).isCell());

    VM& vm = globalObject->vm();
    JSCell* cell = callFrame->uncheckedArgument(0).asCell();
    ASSERT_UNUSED(vm, cell != vm.orderedHashTableSentinel());

    JSMap::Storage& storage = *jsCast<JSMap::Storage*>(cell);
    return JSValue::encode(JSMap::Helper::getIterationEntry(storage));
}

JSC_DEFINE_HOST_FUNCTION(mapPrivateFuncMapIterationEntryKey, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    ASSERT(callFrame->argument(0).isCell());

    VM& vm = globalObject->vm();
    JSCell* cell = callFrame->uncheckedArgument(0).asCell();
    ASSERT_UNUSED(vm, cell != vm.orderedHashTableSentinel());

    JSMap::Storage& storage = *jsCast<JSMap::Storage*>(cell);
    return JSValue::encode(JSMap::Helper::getIterationEntryKey(storage));
}

JSC_DEFINE_HOST_FUNCTION(mapPrivateFuncMapIterationEntryValue, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    ASSERT(callFrame->argument(0).isCell());

    VM& vm = globalObject->vm();
    JSCell* cell = callFrame->uncheckedArgument(0).asCell();
    ASSERT_UNUSED(vm, cell != vm.orderedHashTableSentinel());

    JSMap::Storage& storage = *jsCast<JSMap::Storage*>(cell);
    return JSValue::encode(JSMap::Helper::getIterationEntryValue(storage));
}

JSC_DEFINE_HOST_FUNCTION(mapPrivateFuncMapStorage, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    ASSERT(jsDynamicCast<JSMap*>(callFrame->argument(0)));
    JSMap* map = jsCast<JSMap*>(callFrame->uncheckedArgument(0));
    return JSValue::encode(map->storageOrSentinel(getVM(globalObject)));
}

}
