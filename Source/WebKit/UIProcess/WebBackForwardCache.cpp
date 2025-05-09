/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebBackForwardCache.h"

#include "Logging.h"
#include "SuspendedPageProxy.h"
#include "WebBackForwardCacheEntry.h"
#include "WebBackForwardListItem.h"
#include "WebPageProxy.h"
#include "WebProcessMessages.h"
#include "WebProcessPool.h"
#include "WebProcessProxy.h"
#include "WebsiteDataStore.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(WebBackForwardCache);

WebBackForwardCache::WebBackForwardCache(WebProcessPool& processPool)
    : m_processPool(processPool)
{
}

WebBackForwardCache::~WebBackForwardCache()
{
    clear();
}

void WebBackForwardCache::ref() const
{
    m_processPool->ref();
}

void WebBackForwardCache::deref() const
{
    m_processPool->deref();
}

inline void WebBackForwardCache::removeOldestEntry()
{
    ASSERT(!m_itemsWithCachedPage.isEmptyIgnoringNullReferences());
    if (RefPtr item = m_itemsWithCachedPage.tryTakeFirst())
        item->setBackForwardCacheEntry(nullptr);
}

void WebBackForwardCache::setCapacity(WebProcessPool& pool, unsigned capacity)
{
    if (m_capacity == capacity)
        return;

    m_capacity = capacity;
    while (size() > capacity)
        removeOldestEntry();

    pool.sendToAllProcesses(Messages::WebProcess::SetBackForwardCacheCapacity(m_capacity));
}

void WebBackForwardCache::addEntry(WebBackForwardListItem& item, Ref<WebBackForwardCacheEntry>&& backForwardCacheEntry)
{
    ASSERT(capacity());

    if (item.backForwardCacheEntry()) {
        ASSERT(m_itemsWithCachedPage.contains(item));
        m_itemsWithCachedPage.remove(item);
    }

    item.setBackForwardCacheEntry(WTFMove(backForwardCacheEntry));
    m_itemsWithCachedPage.add(item);

    if (size() > capacity())
        removeOldestEntry();
    ASSERT(size() <= capacity());

    RELEASE_LOG(BackForwardCache, "WebBackForwardCache::addEntry: item=%s, hasSuspendedPage=%d, size=%u/%u", item.identifier().toString().utf8().data(), !!item.suspendedPage(), size(), capacity());
}

void WebBackForwardCache::addEntry(WebBackForwardListItem& item, Ref<SuspendedPageProxy>&& suspendedPage)
{
    auto coreProcessIdentifier = suspendedPage->process().coreProcessIdentifier();
    addEntry(item, WebBackForwardCacheEntry::create(*this, item.identifier(), coreProcessIdentifier, WTFMove(suspendedPage)));
}

void WebBackForwardCache::addEntry(WebBackForwardListItem& item, WebCore::ProcessIdentifier processIdentifier)
{
    addEntry(item, WebBackForwardCacheEntry::create(*this, item.identifier(), WTFMove(processIdentifier), nullptr));
}

void WebBackForwardCache::removeEntry(WebBackForwardListItem& item)
{
    ASSERT(m_itemsWithCachedPage.contains(item));
    m_itemsWithCachedPage.remove(item);
    RELEASE_LOG(BackForwardCache, "WebBackForwardCache::removeEntry: item=%s, size=%u/%u", item.identifier().toString().utf8().data(), size(), capacity());
    item.setBackForwardCacheEntry(nullptr); // item may be dead after this call.
}

void WebBackForwardCache::removeEntry(SuspendedPageProxy& suspendedPage)
{
    removeEntriesMatching([&suspendedPage](auto& item) {
        return item.suspendedPage() == &suspendedPage;
    });
}

Ref<SuspendedPageProxy> WebBackForwardCache::takeSuspendedPage(WebBackForwardListItem& item)
{
    RELEASE_LOG(BackForwardCache, "WebBackForwardCache::takeSuspendedPage: item=%s", item.identifier().toString().utf8().data());

    ASSERT(m_itemsWithCachedPage.contains(item));
    ASSERT(item.backForwardCacheEntry());
    Ref suspendedPage = item.protectedBackForwardCacheEntry()->takeSuspendedPage();
    removeEntry(item);
    return suspendedPage;
}

void WebBackForwardCache::removeEntriesForProcess(WebProcessProxy& process)
{
    removeEntriesMatching([processIdentifier = process.coreProcessIdentifier()](auto& entry) {
        ASSERT(entry.backForwardCacheEntry());
        return entry.backForwardCacheEntry()->processIdentifier() == processIdentifier;
    });
}

void WebBackForwardCache::removeEntriesForSession(PAL::SessionID sessionID)
{
    removeEntriesMatching([sessionID](auto& item) {
        auto* dataStore = item.backForwardCacheEntry()->process()->websiteDataStore();
        return dataStore && dataStore->sessionID() == sessionID;
    });
}

void WebBackForwardCache::removeEntriesForPage(WebPageProxy& page)
{
    removeEntriesMatching([pageID = page.identifier()](auto& item) {
        return item.pageID() == pageID;
    });
}

void WebBackForwardCache::removeEntriesForPageAndProcess(WebPageProxy& page, WebProcessProxy& process)
{
    removeEntriesMatching([pageID = page.identifier(), processIdentifier = process.coreProcessIdentifier()](auto& item) {
        ASSERT(item.backForwardCacheEntry());
        return item.pageID() == pageID && item.backForwardCacheEntry()->processIdentifier() == processIdentifier;
    });
}

void WebBackForwardCache::removeEntriesMatching(NOESCAPE const Function<bool(WebBackForwardListItem&)>& matches)
{
    Vector<Ref<WebBackForwardListItem>> itemsWithEntriesToClear;
    for (Ref item : m_itemsWithCachedPage) {
        if (matches(item))
            itemsWithEntriesToClear.append(WTFMove(item));
    }
    for (Ref item : itemsWithEntriesToClear) {
        m_itemsWithCachedPage.remove(item.get());
        item->setBackForwardCacheEntry(nullptr);
    }
}

void WebBackForwardCache::clear()
{
    RELEASE_LOG(BackForwardCache, "WebBackForwardCache::clear");
    auto itemsWithCachedPage = WTFMove(m_itemsWithCachedPage);
    for (Ref item : itemsWithCachedPage)
        item->setBackForwardCacheEntry(nullptr);
}

void WebBackForwardCache::pruneToSize(unsigned newSize)
{
    RELEASE_LOG(BackForwardCache, "WebBackForwardCache::pruneToSize(%u)", newSize);
    while (size() > newSize)
        removeOldestEntry();
}

} // namespace WebKit.
