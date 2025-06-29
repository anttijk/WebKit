/*
 *  Copyright (C) 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
 *  Copyright (C) 2008 Collabora Ltd.
 *  Copyright (C) 2009 Martin Robinson
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

#pragma once

#if USE(GLIB)

#include <algorithm>
#include <glib.h>
#include <wtf/HashTraits.h>

extern "C" {
    typedef struct _GDBusConnection GDBusConnection;
    typedef struct _GDBusNodeInfo GDBusNodeInfo;

    GDBusNodeInfo* g_dbus_node_info_ref(GDBusNodeInfo*);
    void g_dbus_node_info_unref(GDBusNodeInfo*);

    // Since GLib 2.56 a g_object_ref_sink() macro may be defined which propagates
    // the type of the parameter to the returned value, but it conflicts with the
    // declaration below, causing an error when glib-object.h is included before
    // this file. Thus, add the forward declarations only when the macro is not
    // present.
#ifndef g_object_ref_sink
    void g_object_unref(gpointer);
    gpointer g_object_ref_sink(gpointer);
#endif
};

namespace WTF {

enum GRefPtrAdoptType { GRefPtrAdopt };
template <typename T> inline T* refGPtr(T*);
template <typename T> inline void derefGPtr(T*);
template <typename T> class GRefPtr;
template <typename T> GRefPtr<T> adoptGRef(T*);

// Smart pointer for C types with automatic ref-counting, especially designed for interfacing with glib-based APIs.
// An instance of GRefPtr<T> is either empty or owns a reference to object T.
//
// GRefPtr<T> relies on implementations of refGPtr<T>(), derefGPtr<T>() and adoptGRef<T>().
// The default implementations use g_object_ref_sink() and g_object_unref().
// However, many specializations are available in separate headers such as:
// GRefPtrGStreamer.h, GRefPtrGtk.h and GRefPtrWPE.h.
//
// These specializations allow GRefPtr to work with types that are not derived from GObject,
// such as GstMiniObject, as well as to take advantage of wrappers with improved logging such
// as gst_object_ref() and additional assertions for adoptGRef().
//
// **You must include the header containing any specific specializations you need.**
// Failing to do so will silently cause the default implementation to be used.
template <typename T> class GRefPtr {
public:
    typedef T ValueType;
    typedef ValueType* PtrType;

    GRefPtr() : m_ptr(0) { }

    // Acquires an owning reference of ptr and returns the GRefPtr<T> containing it.
    // This corresponds to the semantics of transfer floating in glib and g_object_ref_sink():
    //
    // * If ptr was a floating reference, it stops being floating and ownership *moves* into GRefPtr;
    //   this corresponds to the same semantics as transfer full.
    // * If ptr was not a floating reference, the ref count is increased to accomodate the new GRefPtr
    //   in addition to any existing owners; this corresponds to the same semantics as transfer none.
    //
    // To transfer ownership of a raw pointer reference that is not floating, see adoptGRef() instead.
    GRefPtr(T* ptr /* (transfer floating) */)
        : m_ptr(ptr)
    {
        if (ptr)
            refGPtr(ptr);
    }

    GRefPtr(const GRefPtr& o)
        : m_ptr(o.m_ptr)
    {
        if (T* ptr = m_ptr)
            refGPtr(ptr);
    }

    template <typename U> GRefPtr(const GRefPtr<U>& o)
        : m_ptr(o.get())
    {
        if (T* ptr = m_ptr)
            refGPtr(ptr);
    }

    GRefPtr(GRefPtr&& o) : m_ptr(o.leakRef()) { }
    template <typename U> GRefPtr(GRefPtr<U>&& o) : m_ptr(o.leakRef()) { }

    ~GRefPtr()
    {
        if (T* ptr = m_ptr)
            derefGPtr(ptr);
    }

    void clear()
    {
        T* ptr = m_ptr;
        m_ptr = 0;
        if (ptr)
            derefGPtr(ptr);
    }

    // Relinquishes the owned reference as a raw pointer. GRefPtr<T> is empty afterwards.
    T* /* (transfer full) */ leakRef() WARN_UNUSED_RETURN
    {
        T* ptr = m_ptr;
        m_ptr = 0;
        return ptr;
    }

    // Increments the reference count.
    T* /* (transfer full) */ ref() WARN_UNUSED_RETURN {
        refGPtr(m_ptr);
        return m_ptr;
    }

    T*& outPtr()
    {
        clear();
        return m_ptr;
    }

    // Hash table deleted values, which are only constructed and never copied or destroyed.
    GRefPtr(HashTableDeletedValueType) : m_ptr(hashTableDeletedValue()) { }
    bool isHashTableDeletedValue() const { return m_ptr == hashTableDeletedValue(); }

    // Borrows the raw pointer from GRefPtr.
    // The pointer is guaranteed to be valid for as long as GRefPtr holds an owning reference
    // to that object.
    T* /* (transfer none) */ get() const LIFETIME_BOUND { return m_ptr; }
    // Same as get(), but clang won't complain if you return the pointer to your callee.
    // Generally unsafe: to use, you must guarantee that the object will still have strong
    // references by the time your function returns.
    // Only used for C API functions returning (transfer none) of objects where the above
    // can be established, e.g. objects stored in a global dictionary.
    T* /* (transfer none) */ getUncheckedLifetime() const { return m_ptr; }
    T& operator*() const { return *m_ptr; }
    ALWAYS_INLINE T* operator->() const { return m_ptr; }

    bool operator!() const { return !m_ptr; }

    // This conversion operator allows implicit conversion to bool but not to other integer types.
    typedef T* GRefPtr::*UnspecifiedBoolType;
    operator UnspecifiedBoolType() const { return m_ptr ? &GRefPtr::m_ptr : 0; }

    GRefPtr& operator=(const GRefPtr&);
    GRefPtr& operator=(GRefPtr&&);
    GRefPtr& operator=(T*);
    template <typename U> GRefPtr& operator=(const GRefPtr<U>&);

    void swap(GRefPtr&);
    friend GRefPtr adoptGRef<T>(T*);

private:
    static T* hashTableDeletedValue() { return reinterpret_cast<T*>(-1); }
    // Adopting constructor.
    GRefPtr(T* ptr, GRefPtrAdoptType) : m_ptr(ptr) {}

    T* m_ptr;
};

template <typename T> inline GRefPtr<T>& GRefPtr<T>::operator=(const GRefPtr<T>& o)
{
    T* optr = o.get();
    if (optr)
        refGPtr(optr);
    T* ptr = m_ptr;
    m_ptr = optr;
    if (ptr)
        derefGPtr(ptr);
    return *this;
}

template <typename T> inline GRefPtr<T>& GRefPtr<T>::operator=(GRefPtr<T>&& o)
{
    GRefPtr ptr = WTFMove(o);
    swap(ptr);
    return *this;
}

template <typename T> inline GRefPtr<T>& GRefPtr<T>::operator=(T* optr)
{
    T* ptr = m_ptr;
    if (optr)
        refGPtr(optr);
    m_ptr = optr;
    if (ptr)
        derefGPtr(ptr);
    return *this;
}

template <class T> inline void GRefPtr<T>::swap(GRefPtr<T>& o)
{
    std::swap(m_ptr, o.m_ptr);
}

template <class T> inline void swap(GRefPtr<T>& a, GRefPtr<T>& b)
{
    a.swap(b);
}

template <typename T, typename U> inline bool operator==(const GRefPtr<T>& a, const GRefPtr<U>& b)
{
    return a.get() == b.get();
}

template <typename T, typename U> inline bool operator==(const GRefPtr<T>& a, U* b)
{
    return a.get() == b;
}

template <typename T, typename U> inline GRefPtr<T> static_pointer_cast(const GRefPtr<U>& p)
{
    return GRefPtr<T>(static_cast<T*>(p.get()));
}

template <typename T, typename U> inline GRefPtr<T> const_pointer_cast(const GRefPtr<U>& p)
{
    return GRefPtr<T>(const_cast<T*>(p.get()));
}

template <typename T> struct IsSmartPtr<GRefPtr<T>> {
    static const bool value = true;
    static constexpr bool isNullable = true;
};

// Transfer ownership of a raw pointer non-floating reference into a new GRefPtr (transfer full semantics).
// This involves no refcount increases or calls to glib.
// This corresponds to the semantics of transfer full in glib.
//
// adoptGRef() must NEVER be used with floating references, as any ref_sink() would cause
// the reference to be stolen from GRefPtr.
//
// Specializations for types where floating references are common should contain debug assertions
// guarding against accidental use with floating references.
template <typename T> GRefPtr<T> adoptGRef(T* p /* (transfer full) */)
{
    return GRefPtr<T>(p, GRefPtrAdopt);
}

template <> WTF_EXPORT_PRIVATE GHashTable* refGPtr(GHashTable* ptr);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GHashTable* ptr);
template <> WTF_EXPORT_PRIVATE GMainContext* refGPtr(GMainContext* ptr);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GMainContext* ptr);
template <> WTF_EXPORT_PRIVATE GMainLoop* refGPtr(GMainLoop* ptr);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GMainLoop* ptr);
template <> WTF_EXPORT_PRIVATE GVariant* refGPtr(GVariant* ptr);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GVariant* ptr);
template <> WTF_EXPORT_PRIVATE GVariantBuilder* refGPtr(GVariantBuilder* ptr);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GVariantBuilder* ptr);
template <> WTF_EXPORT_PRIVATE GSource* refGPtr(GSource* ptr);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GSource* ptr);
template <> WTF_EXPORT_PRIVATE GPtrArray* refGPtr(GPtrArray*);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GPtrArray*);
template <> WTF_EXPORT_PRIVATE GByteArray* refGPtr(GByteArray*);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GByteArray*);
template <> WTF_EXPORT_PRIVATE GBytes* refGPtr(GBytes*);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GBytes*);
template <> WTF_EXPORT_PRIVATE GClosure* refGPtr(GClosure*);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GClosure*);
template <> WTF_EXPORT_PRIVATE GRegex* refGPtr(GRegex*);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GRegex*);
template <> WTF_EXPORT_PRIVATE GMappedFile* refGPtr(GMappedFile*);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GMappedFile*);
template <> WTF_EXPORT_PRIVATE GDateTime* refGPtr(GDateTime* ptr);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GDateTime* ptr);
template <> WTF_EXPORT_PRIVATE GDBusNodeInfo* refGPtr(GDBusNodeInfo* ptr);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GDBusNodeInfo* ptr);
template <> WTF_EXPORT_PRIVATE GArray* refGPtr(GArray*);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GArray*);
template <> WTF_EXPORT_PRIVATE GResource* refGPtr(GResource*);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GResource*);
template <> WTF_EXPORT_PRIVATE GUri* refGPtr(GUri*);
template <> WTF_EXPORT_PRIVATE void derefGPtr(GUri*);

template <typename T> inline T* refGPtr(T* ptr)
{
    if (ptr)
        g_object_ref_sink(ptr);
    return ptr;
}

template <typename T> inline void derefGPtr(T* ptr)
{
    if (ptr)
        g_object_unref(ptr);
}

template<typename P> struct DefaultHash<GRefPtr<P>> : PtrHash<GRefPtr<P>> { };

template<typename P> struct HashTraits<GRefPtr<P>> : SimpleClassHashTraits<GRefPtr<P>> {
    static P* emptyValue() { return nullptr; }

    typedef P* PeekType;
    static PeekType peek(const GRefPtr<P>& value) { return value.get(); }
    static PeekType peek(P* value) { return value; }

    static void customDeleteBucket(GRefPtr<P>& value)
    {
        // See unique_ptr's customDeleteBucket() for an explanation.
        ASSERT(!SimpleClassHashTraits<GRefPtr<P>>::isDeletedValue(value));
        auto valueToBeDestroyed = WTFMove(value);
        SimpleClassHashTraits<GRefPtr<P>>::constructDeletedValue(value);
    }
};

} // namespace WTF

using WTF::GRefPtr;
using WTF::adoptGRef;

#endif // USE(GLIB)
