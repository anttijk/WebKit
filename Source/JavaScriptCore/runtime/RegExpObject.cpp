/*
 *  Copyright (C) 1999-2000 Harri Porten (porten@kde.org)
 *  Copyright (C) 2003-2023 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"
#include "RegExpObject.h"

#include "RegExpObjectInlines.h"

namespace JSC {

STATIC_ASSERT_IS_TRIVIALLY_DESTRUCTIBLE(RegExpObject);

const ClassInfo RegExpObject::s_info = { "RegExp"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(RegExpObject) };

static JSC_DECLARE_CUSTOM_SETTER(regExpObjectSetLastIndexStrict);
static JSC_DECLARE_CUSTOM_SETTER(regExpObjectSetLastIndexSloppy);

RegExpObject::RegExpObject(VM& vm, Structure* structure, RegExp* regExp, bool areLegacyFeaturesEnabled)
    : JSNonFinalObject(vm, structure)
    , m_regExpAndFlags(std::bit_cast<uintptr_t>(regExp) | (areLegacyFeaturesEnabled ? 0 : legacyFeaturesDisabledFlag)) // lastIndexIsNotWritableFlag is not set.
{
    m_lastIndex.setWithoutWriteBarrier(jsNumber(0));
}

#if ASSERT_ENABLED
void RegExpObject::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    ASSERT(inherits(info()));
    ASSERT(type() == RegExpObjectType);
}
#endif

template<typename Visitor>
void RegExpObject::visitChildrenImpl(JSCell* cell, Visitor& visitor)
{
    RegExpObject* thisObject = jsCast<RegExpObject*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    Base::visitChildren(thisObject, visitor);
    visitor.appendUnbarriered(thisObject->regExp());
    visitor.append(thisObject->m_lastIndex);
}

DEFINE_VISIT_CHILDREN(RegExpObject);

bool RegExpObject::getOwnPropertySlot(JSObject* object, JSGlobalObject* globalObject, PropertyName propertyName, PropertySlot& slot)
{
    VM& vm = globalObject->vm();
    if (propertyName == vm.propertyNames->lastIndex) {
        RegExpObject* regExp = jsCast<RegExpObject*>(object);
        unsigned attributes = regExp->lastIndexIsWritable() ? PropertyAttribute::DontDelete | PropertyAttribute::DontEnum : PropertyAttribute::DontDelete | PropertyAttribute::DontEnum | PropertyAttribute::ReadOnly;
        slot.setValue(regExp, attributes, regExp->getLastIndex());
        return true;
    }
    return Base::getOwnPropertySlot(object, globalObject, propertyName, slot);
}

bool RegExpObject::deleteProperty(JSCell* cell, JSGlobalObject* globalObject, PropertyName propertyName, DeletePropertySlot& slot)
{
    VM& vm = globalObject->vm();
    if (propertyName == vm.propertyNames->lastIndex)
        return false;
    return Base::deleteProperty(cell, globalObject, propertyName, slot);
}

void RegExpObject::getOwnSpecialPropertyNames(JSObject*, JSGlobalObject* globalObject, PropertyNameArray& propertyNames, DontEnumPropertiesMode mode)
{
    VM& vm = globalObject->vm();
    if (mode == DontEnumPropertiesMode::Include)
        propertyNames.add(vm.propertyNames->lastIndex);
}

bool RegExpObject::defineOwnProperty(JSObject* object, JSGlobalObject* globalObject, PropertyName propertyName, const PropertyDescriptor& descriptor, bool shouldThrow)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (propertyName == vm.propertyNames->lastIndex) {
        RegExpObject* regExp = jsCast<RegExpObject*>(object);
        if (descriptor.configurablePresent() && descriptor.configurable())
            return typeError(globalObject, scope, shouldThrow, UnconfigurablePropertyChangeConfigurabilityError);
        if (descriptor.enumerablePresent() && descriptor.enumerable())
            return typeError(globalObject, scope, shouldThrow, UnconfigurablePropertyChangeEnumerabilityError);
        if (descriptor.isAccessorDescriptor())
            return typeError(globalObject, scope, shouldThrow, UnconfigurablePropertyChangeAccessMechanismError);
        if (!regExp->lastIndexIsWritable()) {
            if (descriptor.writablePresent() && descriptor.writable())
                return typeError(globalObject, scope, shouldThrow, UnconfigurablePropertyChangeWritabilityError);
            if (descriptor.value()) {
                bool isSame = sameValue(globalObject, regExp->getLastIndex(), descriptor.value());
                RETURN_IF_EXCEPTION(scope, false);
                if (!isSame)
                    return typeError(globalObject, scope, shouldThrow, ReadonlyPropertyChangeError);
            }
            return true;
        }
        if (descriptor.value()) {
            regExp->setLastIndex(globalObject, descriptor.value(), false);
            RETURN_IF_EXCEPTION(scope, false);
        }
        if (descriptor.writablePresent() && !descriptor.writable())
            regExp->setLastIndexIsNotWritable();
        return true;
    }

    RELEASE_AND_RETURN(scope, Base::defineOwnProperty(object, globalObject, propertyName, descriptor, shouldThrow));
}

JSC_DEFINE_CUSTOM_SETTER(regExpObjectSetLastIndexStrict, (JSGlobalObject* globalObject, EncodedJSValue thisValue, EncodedJSValue value, PropertyName))
{
    return jsCast<RegExpObject*>(JSValue::decode(thisValue))->setLastIndex(globalObject, JSValue::decode(value), true);
}

JSC_DEFINE_CUSTOM_SETTER(regExpObjectSetLastIndexSloppy, (JSGlobalObject* globalObject, EncodedJSValue thisValue, EncodedJSValue value, PropertyName))
{
    return jsCast<RegExpObject*>(JSValue::decode(thisValue))->setLastIndex(globalObject, JSValue::decode(value), false);
}

bool RegExpObject::put(JSCell* cell, JSGlobalObject* globalObject, PropertyName propertyName, JSValue value, PutPropertySlot& slot)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    RegExpObject* thisObject = jsCast<RegExpObject*>(cell);

    if (propertyName == vm.propertyNames->lastIndex) {
        if (!thisObject->lastIndexIsWritable())
            return typeError(globalObject, scope, slot.isStrictMode(), ReadonlyPropertyWriteError);

        if (slot.thisValue() != thisObject) [[unlikely]]
            RELEASE_AND_RETURN(scope, JSObject::definePropertyOnReceiver(globalObject, propertyName, value, slot));

        bool result = thisObject->setLastIndex(globalObject, value, slot.isStrictMode());
        RETURN_IF_EXCEPTION(scope, false);
        slot.setCustomValue(thisObject, slot.isStrictMode()
            ? regExpObjectSetLastIndexStrict
            : regExpObjectSetLastIndexSloppy);
        return result;
    }
    RELEASE_AND_RETURN(scope, Base::put(cell, globalObject, propertyName, value, slot));
}

JSValue RegExpObject::exec(JSGlobalObject* globalObject, JSString* string)
{
    return execInline(globalObject, string);
}

// Shared implementation used by test and exec.
MatchResult RegExpObject::match(JSGlobalObject* globalObject, JSString* string)
{
    return matchInline(globalObject, string);
}

JSValue RegExpObject::matchGlobal(JSGlobalObject* globalObject, JSString* string)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    RegExp* regExp = this->regExp();

    ASSERT(regExp->global());

    setLastIndex(globalObject, 0);
    RETURN_IF_EXCEPTION(scope, { });

    if (regExp->hasValidAtom()) {
        if (string->isSubstring()) {
            auto& cache = globalObject->regExpGlobalData().substringGlobalAtomCache();
            RELEASE_AND_RETURN(scope, cache.collectMatches(globalObject, string->asRope(), regExp));
        }
        RELEASE_AND_RETURN(scope, collectGlobalAtomMatches(globalObject, string, regExp));
    }

    auto s = string->view(globalObject);
    RETURN_IF_EXCEPTION(scope, { });

    ASSERT(!s->isNull());
    if (regExp->eitherUnicode()) {
        unsigned stringLength = s->length();
        RELEASE_AND_RETURN(scope, collectMatches(
            vm, globalObject, string, s, regExp,
            [&](size_t end) ALWAYS_INLINE_LAMBDA {
                return advanceStringUnicode(s, stringLength, end);
            }));
    }

    RELEASE_AND_RETURN(scope, collectMatches(
        vm, globalObject, string, s, regExp,
        [](size_t end) ALWAYS_INLINE_LAMBDA {
            return end + 1;
        }));
}

} // namespace JSC
